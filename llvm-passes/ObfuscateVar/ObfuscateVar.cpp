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
         - A = X%10
         - B = X//10
       
         Z=X+Y will be transform into :
         - Z_A = (X_A+Y_A) mod 10
         - Z_B = { 10*(X_B+Y_B)+[(X_A+Y_A) mod 10] } div 10

         To rebuild the variable : 
         X = 10*X_B+X_A

      */
    
      for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),end = BB.end(); I != end; ++I) {
      
        Instruction &Inst = *I;
        bool isVolatile = false;
      
        if(isValidInstForSplit(Inst)) {//Check if the instruction can be splited
        
          for (size_t i=0; i < Inst.getNumOperands(); ++i) {
            if (Value *V = isValidCandidateOperand(Inst.getOperand(i))) {

              if(!isSplited(V)){//Check if the operand is splited
                dbgs() << *V << " isn't splitted\n";
                splitVariable(V,Inst);
                //dbgs() << *V << " is now splited\n";
                
              }

            }//isValidCandidateOperand
          }//for

          if(BinaryOperator *Binop = dyn_cast<BinaryOperator>(&Inst)) {

            //Get the operator's operands
            Value *op0 = parseOperand(Binop->getOperand(0));
            Value *op1 = parseOperand(Binop->getOperand(1));
           
            if(!(isSplited(op0) && isSplited(op1))){//Check if the two operands are splited. It should be ok due to the previous loop
              dbgs() << "Error : operands aren't splited !\n";
              break;
            }
           
            switch(Binop->getOpcode()){
              
            case Instruction::Add :{//Split add instruction

              dbgs() << "Split ADD instruction\n";
              
              IRBuilder<> Builder(&Inst);
              Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    
              Constant *C10 = ConstantInt::get(IntermediaryType,10,false);
              
              // Get X_A and X_B from the first operand
              typeMap::iterator it=varsRegister.find(op0); 
              
              Value *op0_A = Builder.CreateLoad(it->second.first,isVolatile);
              Value *op0_B = Builder.CreateLoad(it->second.second,isVolatile);

              // Get Y_A and Y_B from the second operand
              it=varsRegister.find(op1);
              Value *op1_A = Builder.CreateLoad(it->second.first,isVolatile);
              Value *op1_B = Builder.CreateLoad(it->second.second,isVolatile);

              
              Value* AddA0A1 = Builder.CreateAdd(op0_A,op1_A);//X_A+Y_A
              Value* AddB0B1 = Builder.CreateAdd(op0_B,op1_B);//X_B+Y_B
              
              Value* RemA0A1 = Builder.CreateURem(AddA0A1,C10,"Z_A");//X_A+Y_A mod 10 => Z_A

              Value* MulB0B1 = Builder.CreateMul(AddB0B1,C10);//10*(X_B+Y_B)
              
              Value* AddAB = Builder.CreateAdd(MulB0B1,RemA0A1);//10*(X_B+Y_B)+[X_A+Y_A mod 10]

              Value* DivAB = Builder.CreateUDiv(AddAB,C10,"Z_B"); //{ 10*(X_B+Y_B)+[(X_A+Y_A) mod 10] } div 10 => Z_B

              //Allocate two register to store the splited result
              Value* alloResultA = Builder.CreateAlloca(IntermediaryType,nullptr,"a_res");
              Value* alloResultB = Builder.CreateAlloca(IntermediaryType,nullptr,"b_res");

              Value* StoreResultA = Builder.CreateStore(RemA0A1,alloResultA,isVolatile);
              Value* StoreResultB = Builder.CreateStore(DivAB,alloResultB,isVolatile);
 
              Value* LoadResultA = Builder.CreateLoad(alloResultA,isVolatile);

              // the key to access to the variables Z_A and Z_B from the ValueMap is Z_A.
              // It's a convention and we could choose Z_B
              varsRegister[parseOperand(LoadResultA)] = std::make_pair(alloResultA,alloResultB);

              // We replace all uses of the add result with the register that contains Z_A
              Inst.replaceAllUsesWith(LoadResultA);
              
              //Inst.eraseFromParent();
              // for (User *U : Inst.users()) {
              //   if (Instruction *Inst2 = dyn_cast<Instruction>(U)) {
              //     errs() << "F is used in instruction:\n";
              //     errs() << *Inst2 << "\n";
              //   }
              // }
              
              
              break;

            }
            case Instruction::Sub :
              {
                //To implement
                break;
              }
            default: break;
              
            }
          }

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
            //TODO
            //Other case ?
            
          
          }
        }

      

      
      }


      return true;
    }

  private:

    /*
     * Merge the variable V (which is an operand) for the instruction Inst
     */
    Value *mergeVariable(Value *Var,Instruction &Inst){
      typeMap::iterator it;
      Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    
      Constant *C10 = ConstantInt::get(IntermediaryType,10,false);

      if((it=varsRegister.find(Var)) != varsRegister.end()){// Check if the variable is splited
        
        IRBuilder<> Builder(&Inst);
        //dbgs() << "We should merge : " << *op << "\n";
        //Get X_A X_B
        Value *A = Builder.CreateLoad(it->second.first);
        Value *B = Builder.CreateLoad(it->second.second);
        
        Value* M10B = Builder.CreateMul(B,C10);//10*B
        Value* res = Builder.CreateAdd(M10B,A,"add_final");//10*B+A
        return res; 
      
       
      }else{
        return nullptr;
      }
    
    }

    /*
     * Check if the instruction Inst is valid to be splited
     */
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


    /*
     * Check if the instruction Inst is valid to be merged
     */
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

    /*
     * Check if the value can be splited
     */
    Value *isValidCandidateOperand(Value *V) {
  
      if (Constant *C = dyn_cast<Constant>(V)) {
        
        if (isa<PointerType>(V->getType())) {
          return nullptr;
        } else if (V->getType()->isFloatingPointTy()) {
          return nullptr;
        } else if (V->getType()->isIntegerTy()) {
          return V;
        } else {
          return nullptr;
        }

      }else if(isa<LoadInst>(V)){
        return V;
      }else{
        return nullptr;
      }
      
     
  
  
    }

    
    Value* parseOperand(Value* V){
      if(LoadInst *loadInst = dyn_cast<LoadInst>(V)){
        return loadInst->getPointerOperand();
      }else{
        return V;
      }
        
    }

    //Return true if the value is splited else false
    bool isSplited(Value* V){
      return varsRegister.count(V) == 1;
    }

    /*
     * Split the variable V in the instruction Inst but don't change the operands
     */
    void splitVariable(Value* V,Instruction &Inst){
      
      bool isVolatile = false;
      
      Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
    
      Constant *C10 = ConstantInt::get(IntermediaryType,10,false);
      
      IRBuilder<> Builder(&Inst);

      // Allocate 2 registers for X_A and X_B
      Value* alloA = Builder.CreateAlloca(IntermediaryType,0,"X_A");
      Value* alloB = Builder.CreateAlloca(IntermediaryType,0,"X_B");

      // Store V in X_A and X_B
      Value* StoreA = Builder.CreateStore(V,alloA,isVolatile);
      Value* StoreB = Builder.CreateStore(V,alloB,isVolatile);

      //Load the value
      Value* LoadA = Builder.CreateLoad(alloA,isVolatile);
      Value* LoadB = Builder.CreateLoad(alloB,isVolatile);

      
      Value* ARem = Builder.CreateURem(LoadA,C10); //X_A mod 10
      Value* BDiv = Builder.CreateUDiv(LoadB,C10); //X_B div 10

      Builder.CreateStore(ARem,alloA,isVolatile);
      Builder.CreateStore(BDiv,alloB,isVolatile);

      Value* mapKey = parseOperand(V); 

      // Register X_A and X_B associated with V
      varsRegister[mapKey] = std::make_pair(alloA,alloB);
      
      dbgs() << "We splited : " << *mapKey << "\n";


    
    
    }
    

    /*
     * Attributes
     */
    typeMap varsRegister; //Adresse , (x1,x2)
    //Type *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),sizeof(typeInter)*8);//32bits
  
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
