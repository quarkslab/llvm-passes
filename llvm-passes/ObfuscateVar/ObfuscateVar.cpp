//Compile : cmake -DLLVM_ROOT=$HOME/Documents/Programmation/Obfuscation/llvm/build ..
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#ifndef NDEBUG
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#endif

#include <map>
#include <utility> //std::pair, std::make_pair
#include <random>


using namespace llvm;

namespace {
  typedef uint32_t typeInter;
  typedef std::pair<AllocaInst*,AllocaInst*> typeMapValue;
  typedef std::map<Value*,typeMapValue> typeMap;
  
class ObfuscateVar : public BasicBlockPass {
public:
  
  static char ID;
  ObfuscateVar() : BasicBlockPass(ID) {}

  virtual bool runOnBasicBlock(BasicBlock &BB) override {
    /* 
       Principe de l'obfuscation (d'après https://www.cs.ox.ac.uk/files/2936/RR-10-02.pdf):
       Etant donné une variable X, on l'a "split" en deux variables selon
       la formule :
       - x1 = X%10
       - x2 = X//10
       
       X=X+a sera donc transformé en :
       - x2 = (10*x2+x1+a)//10
       - x1 = x1+a mod 10

       Pour "reconstruire" la variable : 
       X = 10*x2+x1

    */
    
    for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),end = BB.end(); I != end; ++I) {
      
      Instruction &Inst = *I;
      if(BinaryOperator *Binop = dyn_cast<BinaryOperator>(&Inst)) {
        
        if(Binop->getOpcode() == Instruction::Add){
          
          if (isa<Constant>(Binop->getOperand(1))) {// add X,#CONSTANT
            
            if(Value* v = replaceAddOp(Inst)){
              Inst.replaceAllUsesWith(v);
            }

          }//TODO : add X,Y for all X,Y
        }//TODO : sub,div,mul...
          
         
      }else if(StoreInst *sreInst = dyn_cast<StoreInst>(&Inst)){
        if(!isa<PointerType>(sreInst->getValueOperand()->getType())){
          dbgs() << "Store Instruction: "<<*sreInst << "\n";
          registerVar(*sreInst);
        }
        
      }
    }

    return true;
  }

private:

  // Enregistre dans une map les x1,x2 associés a une variable
  // TODO : return value* and replace
  void registerVar(StoreInst &S){
    bool isVolatile = false;
    dbgs() << "operand value = " << *S.getValueOperand() << "\n";
      
    BasicBlock *BB = S.getParent();
    Type *IntermediaryType = IntegerType::get(BB->getContext(),sizeof(typeInter)*8);
    Value *varPointer = S.getPointerOperand();
      
    //dbgs() << "Store Inst :\n\t" << S << "\n\tValue is : " << *C0 <<  "\n\Address is : " << *varPointer << "\n\n";

    AllocaInst* alloX1 = new AllocaInst(IntermediaryType, 0,"x1");
    AllocaInst* alloX2 = new AllocaInst(IntermediaryType, 0,"x2");

    BB->getInstList().insert(S, alloX1); //%x1 = alloca i32, align 4
    BB->getInstList().insert(S, alloX2); //%x2 = alloca i32, align 4

    //Value* valueOperand = S.getValueOperand();
    //dbgs() << "Type: " << *valueOperand->getType() << "\n\n";

    // Initialise x1 et x2 avec avec la "valeur" de la variable a spliter.
    StoreInst *storeX1 = new StoreInst(S.getValueOperand(),alloX1,isVolatile,4);
    StoreInst *storeX2 = new StoreInst(S.getValueOperand(),alloX2,isVolatile,4);
    BB->getInstList().insert(S, storeX1); //store VAR, i32* %x1, align 4
    BB->getInstList().insert(S, storeX2); //store VAR, i32* %x2, align 4

    //Transfert x1,x2 dans des registres pour y effectuer des operations
    LoadInst* loadX1 = new LoadInst(alloX1,"loadX1",isVolatile); 
    LoadInst* loadX2 = new LoadInst(alloX2,"loadX2",isVolatile); 
    BB->getInstList().insert(S, loadX1); //%xx = load i32* %x1
    BB->getInstList().insert(S, loadX2); //%yy = load i32* %x2

    // x1 et x2 sont mis a jour avec les bonnes valeurs.
    // x1 = x1%10
    // x2 = (int)x2/10
    BinaryOperator* opRem = BinaryOperator::Create(Instruction::URem, loadX1, ConstantInt::get(IntermediaryType,10,false));
    BinaryOperator* opDiv = BinaryOperator::Create(Instruction::UDiv, loadX2, ConstantInt::get(IntermediaryType,10,false));
      
    BB->getInstList().insert(S, opRem); //x1%10
    BB->getInstList().insert(S, opDiv); //x2/10

    // Sauvegarde les valeurs de x1,x2 precedement calculées
    storeX1 = new StoreInst(opRem,alloX1,isVolatile,4);
    storeX2 = new StoreInst(opDiv,alloX2,isVolatile,4);
    BB->getInstList().insert(S, storeX1);
    BB->getInstList().insert(S, storeX2);

    // Enregistre les pointeurs de x1,x2 associé a la variable à diviser.
    varsRegister.insert(std::pair<Value*,typeMapValue >(varPointer,std::make_pair(alloX1,alloX2)));
 
  }
  
  Value* replaceAddOp(Instruction &Inst){
    bool isVolatile = false;
    Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    BinaryOperator *Binop = dyn_cast<BinaryOperator>(&Inst);
    ConstantInt *C = dyn_cast<ConstantInt>(Binop->getOperand(1));
    
    Value *var = Binop->getOperand(0); //adresse de la variable

    typeMap::iterator it; // Iterateur sur la map
    
    LoadInst *load = dyn_cast<LoadInst>(&*var);
    Value* varPointer = load->getPointerOperand(); // Pointeur sur la variable de l'operande
    
    //dbgs() << Inst << "\n";
    //dbgs() <<"Load instruction :"<<*load<<" Name is : "<<*load->getPointerOperand()<<"\n";

    if((it=varsRegister.find(varPointer)) == varsRegister.end()){
      
      //Dans ce cas, nous avons une variable qui n'a pas été enregistrée
      
      dbgs() << "Erreur : la variable" << *varPointer << " n'a pas été enregistrée\n";
      
      return NULL;
      
    }
    
    Constant *C10 = ConstantInt::get(IntermediaryType,10,false); 
    
    AllocaInst *alloX1 = dyn_cast<AllocaInst>(&*it->second.first);
    AllocaInst *alloX2 = dyn_cast<AllocaInst>(&*it->second.second);
    
    IRBuilder<> Builder(&Inst);

    Value* lx1 = Builder.CreateLoad(alloX1,isVolatile,"lx1");
    Value* lx2 = Builder.CreateLoad(alloX2,isVolatile,"lx2");


    Value* X1Add = Builder.CreateAdd(lx1,C);
    
    Value* X1Rem = Builder.CreateURem(X1Add,C10);

    Value* X2Mul = Builder.CreateMul(lx2,C10);
    Value* X2Add = Builder.CreateAdd(X2Mul,X1Add);
    Value* X2Div = Builder.CreateUDiv(X2Add,C10);
    

    Value* Mul = Builder.CreateNUWMul(X2Div,C10);
    Value* MulAdd = Builder.CreateAdd(Mul,X1Rem);

    Value* v = Builder.CreateZExt(MulAdd, IntermediaryType);
    
    return v;

    
  }
  
  typeMap varsRegister; //Adresse , (x1,x2)
  
};
}

char ObfuscateVar::ID = 0;
static RegisterPass<ObfuscateVar> X("ObfuscateVar", "Variable splitting",
                                    false, false);

// register pass for clang use
static void registerObfuscateVarPass(const PassManagerBuilder &,
                                     PassManagerBase &PM) {
  PM.add(new ObfuscateVar());
}
static RegisterStandardPasses
RegisterMBAPass(PassManagerBuilder::EP_EarlyAsPossible,
                registerObfuscateVarPass);
