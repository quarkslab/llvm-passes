//Compile : cmake -DLLVM_ROOT=$HOME/Documents/Programmation/Obfuscation/llvm/build ..
#include <map>
#include <utility> //std::pair, std::make_pair
#include <random>

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/ValueMap.h"

#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#ifndef NDEBUG
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#endif

using namespace llvm;

namespace {
  typedef uint32_t typeInter;
  typedef std::pair<Value*,Value*> typeMapValue;
  typedef ValueMap<Value*,typeMapValue> typeMap;
  
class ObfuscateVar : public BasicBlockPass {
public:
  
  static char ID;
  ObfuscateVar() : BasicBlockPass(ID) {}

  virtual bool runOnBasicBlock(BasicBlock &BB) override {
    /* 
       From https://www.cs.ox.ac.uk/files/2936/RR-10-02.pdf:
       Given a variable X, we split it such as
       - x1 = X%10
       - x2 = X//10
       
       X=X+a will be transform into :
       - x2 = (10*x2+x1+a)//10
       - x1 = x1+a mod 10

       To rebuild the variable : 
       X = 10*x2+x1

    */
    
    for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),end = BB.end(); I != end; ++I) {
      
      Instruction &Inst = *I;
      bool isVolatile = false;
      if(isValidInstForSplit(Inst)) {
        
          for (size_t i=0; i < Inst.getNumOperands(); ++i) {
            if (Value *V = isValidCandidateOperand(Inst.getOperand(i))) {

              if(!isSplited(V)){//Check if the operand is splited
                dbgs() << *V << " isn't splitted\n";
                Value *VS = splitVariable(V,Inst);
                //dbgs() << *V << " is now splited\n";
                
              }

              

              
              // if (Value *New_val = replaceZero(Inst, C)) {
              //   Inst.setOperand(i, New_val);
              //   modified = true;
              // } else {
              //   //dbgs() << "ObfuscateZero: could not rand pick a variable for replacement\n";
              // }
            }//isValidCandidateOperand
          }//for

          if(BinaryOperator *Binop = dyn_cast<BinaryOperator>(&Inst)) {
           Value *op0 = parseOperand(Binop->getOperand(0));
           Value *op1 = parseOperand(Binop->getOperand(1));
           if(!(isSplited(op0) && isSplited(op1))){
             dbgs() << "Error : operands aren't splited !\n";
             break;
           }
           
            switch(Binop->getOpcode()){
            case Instruction::Add :{
              IRBuilder<> Builder(&Inst);
              Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    
              Constant *C10 = ConstantInt::get(IntermediaryType,10,false); 
              typeMap::iterator it=varsRegister.find(op0);
              
              Value *op0_A = Builder.CreateLoad(it->second.first,isVolatile);
              Value *op0_B = Builder.CreateLoad(it->second.second,isVolatile);
              it=varsRegister.find(op1);
              Value *op1_A = Builder.CreateLoad(it->second.first,isVolatile);
              Value *op1_B = Builder.CreateLoad(it->second.second,isVolatile);

              
              Value* AddA0A1 = Builder.CreateAdd(op0_A,op1_A);
              Value* AddB0B1 = Builder.CreateAdd(op0_B,op1_B);
              
              Value* RemA0A1 = Builder.CreateURem(AddA0A1,C10);

              Value* MulB0B1 = Builder.CreateMul(AddB0B1,C10);
              
              Value* AddAB = Builder.CreateAdd(MulB0B1,RemA0A1);

              Value* DivAB = Builder.CreateUDiv(AddAB,C10);


              //Value* alloResult = Builder.CreateAlloca(IntermediaryType,nullptr,"res");
              Value* M10AB = Builder.CreateMul(DivAB,C10);
              Value* result = Builder.CreateAdd(M10AB,RemA0A1);
              


              
              Value* alloResultA = Builder.CreateAlloca(IntermediaryType,nullptr,"a_res");
              Value* alloResultB = Builder.CreateAlloca(IntermediaryType,nullptr,"b_res");
              //Value* X1Rem = Builder.CreateURem(C,C10);
              Value* StoreResultA = Builder.CreateStore(RemA0A1,alloResultA,isVolatile);
              Value* StoreResultB = Builder.CreateStore(DivAB,alloResultB,isVolatile);
              //Value* FakeInstr = Builder.CreateAdd(C10,C10);
              Value* LoadResultA = Builder.CreateLoad(alloResultA,isVolatile);
              //varsRegister[alloResultA] = std::make_pair(alloResultA,alloResultB);
              varsRegister[parseOperand(LoadResultA)] = std::make_pair(alloResultA,alloResultB);
              dbgs() << "op0 = " << *op0 << " - " << varsRegister.count(op0) <<" op1 = " << *op1 << "\n";
              //ValueHandleBase::ValueIsRAUWd(&Inst,LoadResultA);

              // for (Value::use_iterator ui = Inst.use_begin(), e = Inst.use_end(); ui != e; ++ui){
                
              //   //dbgs() << *ui << "\n";
              //   if (Instruction *Inst_tmp = dyn_cast<Instruction>(*ui)) {
              //     dbgs() << "F is used in instruction:\n";
              //     dbgs() << *Inst_tmp << "\n";
              //   }
                
              // }

              
              //ReplaceInstWithValue(Inst.getParent()->getInstList(), I,result);
              //ReplaceInstWithValue(Inst.getParent()->getInstList(), I,alloResultA);
              Inst.replaceAllUsesWith(LoadResultA);
              //Inst.eraseFromParent();
              // for (User *U : Inst.users()) {
              //   if (Instruction *Inst2 = dyn_cast<Instruction>(U)) {
              //     errs() << "F is used in instruction:\n";
              //     errs() << *Inst2 << "\n";
              //   }
              // }
              dbgs() << "Split ADD instruction\n";
              
              break;

            }
            case Instruction::Sub :{break;}
            default: break;
              
          }
          }
          
        
          
          // if (isa<Constant>(Binop->getOperand(1))) {// add X,#CONSTANT
            
          //   if(Value* v = replaceAddOp(Inst)){
          //     Inst.replaceAllUsesWith(v);
          //   }

          // }//TODO : add X,Y for all X,Y
        
          //Inst.removeFromParent();
          
          //ReplaceInstWithValue(Inst.getParent()->getInstList(), I,ConstantInt::get(IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8),10,false));
          
          //break;
          
      
      }else if(isValidInstForMerge(Inst)){
        dbgs() << "Merge : " << Inst << "\n";
        typeMap::iterator it;
        if(isa<StoreInst>(&Inst)){
          Value *op = parseOperand(Inst.getOperand(0));
          if((it=varsRegister.find(op)) != varsRegister.end()){
              dbgs() << "We should merge : " << *op << "\n";
              Value *op_A = it->second.first;
              Value *op_B = it->second.second;
              dbgs() << "\t\t" << *op_A << "|" << *op_B << "\n";
              if(Value *VReplace = mergeVariable(op,Inst)){
                dbgs() << "VReplace = " << *VReplace << "\n";
                Inst.setOperand(0, VReplace);
              }
             
          }
        }else{
          
          // for (size_t i=0; i < Inst.getNumOperands(); ++i) {
            
        //     Value *op = parseOperand(Inst.getOperand(i));
        //     if((it=varsRegister.find(op)) != varsRegister.end()){
        //       dbgs() << "We should merge : " << *op << "\n";
        //       Value *op_A = it->second.first;
        //       Value *op_B = it->second.second;
        //       dbgs() << "\t\t" << *op_A << "|" << *op_B << "\n";
        //       if(Value *VReplace = mergeVariable(op,Inst)){
        //         dbgs() << "VReplace = " << *VReplace << "\n";
        //         Inst.setOperand(i, VReplace);
        //       }
             
        //     }
        // }
          //(merge(Inst.getOperand(i))
          //Check if the operand is splited
          //if yes, merge it
          
        }
      }// else if(StoreInst *sreInst = dyn_cast<StoreInst>(&Inst)){
      //   if(!isa<PointerType>(sreInst->getValueOperand()->getType())){
      //     //dbgs() << "Store Instruction: "<<*sreInst << "\n";
      //     //registerVar(*sreInst);
      //   }
        
      // }

      

      
    }

    
    

    return true;
  }

private:

  Value *mergeVariable(Value *Var,Instruction &Inst){
    typeMap::iterator it;
    Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    
    Constant *C10 = ConstantInt::get(IntermediaryType,10,false);
    Constant *C1 = ConstantInt::get(IntermediaryType,1,false);
    if((it=varsRegister.find(Var)) != varsRegister.end()){
      IRBuilder<> Builder(&Inst);
      //dbgs() << "We should merge : " << *op << "\n";
      Value *A = Builder.CreateLoad(it->second.first);
      Value *B = Builder.CreateLoad(it->second.second);
      Value* M10B = Builder.CreateMul(B,C10);
      Value* res = Builder.CreateAdd(M10B,A,"add_final");//10B*A
      return res;
      
       
    }else{
      return nullptr;
    }
    
  }
  
  bool isValidInstForSplit(Instruction &Inst) {
    switch(Inst.getOpcode()){
    case Instruction::Add:
    case Instruction::Sub:
      return true;
      break;
    default:
      return false;
    }
  }

  bool isValidInstForMerge(Instruction &Inst) {
    if(isa<TerminatorInst>(&Inst)) {
      //dbgs() << "Merge : " << Inst << "\n";
      return true;
    }else if(isa<StoreInst>(&Inst)){
      return true;
    }else if(isa<LoadInst>(&Inst)){
      return false;
    }else if(isa<ReturnInst>(&Inst)){
      return true;
    }else{
      return false;
    }
  }

  Value *isValidCandidateOperand(Value *V) {
  
    if (Constant *C = dyn_cast<Constant>(V)) {
      // Checking constant eligibility
      if (isa<PointerType>(V->getType())) {
        //dbgs() << "Ignoring NULL pointers\n";
        return nullptr;
      } else if (V->getType()->isFloatingPointTy()) {
        //dbgs() << "Ignoring Floats 0\n";
        return nullptr;
      } else if (V->getType()->isIntegerTy()) {
        dbgs() << "Is Integer\n";
        return V;
      } else {
        return nullptr;
      }

    }else if(isa<LoadInst>(V)){
      return V;
    }else{
      return nullptr;
    }
      
      //} else {
      //dbgs() << V->getType() << "\n";
      //return nullptr;
      //}
  
  
  }

  Value* parseOperand(Value* V){
    if(LoadInst *loadInst = dyn_cast<LoadInst>(V)){
      return loadInst->getPointerOperand();
    }else{
      return V;
    }
        
  }
  bool isSplited(Value* V){
    return varsRegister.count(V) == 1;
  }

  Value* splitVariable(Value* V,Instruction &Inst){
    bool isVolatile = false;
    Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    
    Constant *C10 = ConstantInt::get(IntermediaryType,10,false); 
    IRBuilder<> Builder(&Inst);
    //Instruction *C = cast<Instruction>(V);
    
    //dbgs() << C->getOperand() << "\n";
    //AllocaInst *alloX1 = dyn_cast<AllocaInst>(in);
  

    Value* alloA = Builder.CreateAlloca(IntermediaryType,0,"a");
    Value* alloB = Builder.CreateAlloca(IntermediaryType,0,"b");
    //Value* X1Rem = Builder.CreateURem(C,C10);
    Value* StoreA = Builder.CreateStore(V,alloA,isVolatile);
    Value* StoreB = Builder.CreateStore(V,alloB,isVolatile);

    Value* LoadA = Builder.CreateLoad(alloA,isVolatile,"lA");
    Value* LoadB = Builder.CreateLoad(alloB,isVolatile,"lB");

    Value* ARem = Builder.CreateURem(LoadA,C10);
    Value* BDiv = Builder.CreateUDiv(LoadB,C10);

    Value* StoreARem = Builder.CreateStore(ARem,alloA,isVolatile);
    Value* StoreBRem = Builder.CreateStore(BDiv,alloB,isVolatile);

    Value* mapKey = parseOperand(V); 
    
      //dbgs() << "CONST\n" << *V << "\n";
      //Value* IntToPtr = Builder.CreatePtrToInt(C,IntermediaryType,"test");
      
    
    
    varsRegister[mapKey] = std::make_pair(alloA,alloB);
    dbgs() << "We splited : " << *mapKey << "\n";
    //varsRegister.insert(std::pair<Value*,typeMapValue >(mapKey,std::make_pair(alloA,alloB)));
    
    

   
    //return nullptr;
    return nullptr;
    
    
  }
  // We register x1,x2 in a map where the key is the variable
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

    // Initialise x1 and x2 avec avec la "valeur" de la variable a spliter.
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

    typeMap::iterator it; // Iterator on the map
    
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
