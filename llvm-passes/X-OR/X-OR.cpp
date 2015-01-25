#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#ifndef NDEBUG
#include "llvm/IR/Verifier.h"
#endif

#include "llvm/Support/Debug.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#include <numeric>
#include <tuple>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <iostream>

using namespace llvm;

namespace {
    class X_OR: public BasicBlockPass {
        public:

            static char ID;

            X_OR() : BasicBlockPass(ID) {
            }

            virtual bool runOnBasicBlock(BasicBlock &BB) {
                static size_t Counter = 0;
                bool modified = false;
                // TransfoRegister.clear();

                for(typename BasicBlock::iterator I = BB.getFirstInsertionPt(), end = BB.end(); I != end; ++I) {
                    Instruction &Inst = *I;
                    // Checking Instruction eligibility
                    if(BinaryOperator *op = dyn_cast<BinaryOperator>(&Inst)) {
                        if(op->getOpcode() == Instruction::BinaryOps::Xor) {
                            if(Counter++ % 5 != 0)
                                continue;

                            IRBuilder<> Builder(&Inst);

                            const unsigned NewBase = chooseBase(op->getOperand(0)->getType()->getIntegerBitWidth());

                            // Rewritting operands in new base
                            Value *NewOperand1 = rewriteAsBaseN(op->getOperand(0), NewBase,
                                                                Builder, BB.getContext()),
                                  *NewOperand2 = rewriteAsBaseN(op->getOperand(1), NewBase,
                                                                Builder, BB.getContext());

                            // If sthg went wrong abort
                            if(!(NewOperand1 and NewOperand2)) {
                                dbgs() << "X-OR: xor obfuscation failed\n";
                                continue;
                            }

                            Value *NewValue = Builder.CreateAdd(NewOperand1, NewOperand2);

                            // Preparing the result in base 2 for later use
                            Value *InvertResult = rewriteAsBase2(NewValue, NewBase,
                                                                 Inst.getType(), Builder);

                            // TransfoRegister.emplace(NewValue, std::make_pair(Inst.getType(), NewValue->getType()));

                            // Casting back for non XOR operation on NewValue
                            // FIXME : In case of obuscated XOR replace by the obfuscated add return value
                            //         Use TransfoRegister
                            Value *OriginalValue = dyn_cast<Value>(&Inst);
                            // dbgs() << *OriginalValue << "\n";
                            for(auto const& NVUse: OriginalValue->uses()) {
                                Instruction *UseInst = dyn_cast<Instruction>(NVUse.getUser());
                                if(not UseInst)
                                    continue;
                                dbgs() << "\t" << *UseInst << "\n";
                                for(unsigned I = 0; I < UseInst->getNumOperands(); ++I) {
                                    if(UseInst->getOperand(I) == (Value*) &Inst) {
                                        dbgs() << "Replacing : " << *(UseInst->getOperand(I)) << " In " << *UseInst << "\n";
                                        UseInst->setOperand(I, InvertResult);
                                    }
                                }
                            }

                            // Removing useless original XOR
                            // FIXME: Should we leave that to the optimizer?
                            // Inst.eraseFromParent();

                            modified = true;
                        }
                    }
                }
#ifndef NDEBUG
                verifyFunction(*BB.getParent());
#endif
                return modified;
            }

        private:

            // FIXME: 128 bits limit shouldn't be hardcoded
            const unsigned MaxSupportedSize = 128;
            std::default_random_engine Generator;
            // std::unordered_map<Value*, std::pair<Type*, Type*>> TransfoRegister;

            Value *rewriteAsBaseN(Value *Operand, unsigned Base, IRBuilder<> &Builder, LLVMContext &Context) {
                if(!Operand->getType()->isIntegerTy())
                    return nullptr;

                const unsigned OriginalNbBit = Operand->getType()->getIntegerBitWidth(),
                               NewNbBit = requiredBits(OriginalNbBit, Base);
                if(!NewNbBit)
                    return nullptr;

                Type *NewBaseType = IntegerType::get(Context, NewNbBit);

                Constant *IRBase = ConstantInt::get(NewBaseType, Base);
                // Initializing variables
                Value *Accu = ConstantInt::get(NewBaseType, 0),
                      *Mask = ConstantInt::get(NewBaseType, 1),
                      *Pow = ConstantInt::get(NewBaseType, 1);

                // Extending the original value to NewNbBit for bitwise and
                Value *ExtendedOperand = Builder.CreateZExt(Operand, NewBaseType);

                for(unsigned Bit = 0; Bit < OriginalNbBit; ++Bit) {
                    // Updating NewValue
                    Value *MaskedNewValue = Builder.CreateAnd(ExtendedOperand, Mask);
                    Value *BitValue = Builder.CreateLShr(MaskedNewValue, Bit);
                    Value *NewBit = Builder.CreateMul(BitValue, Pow);
                    Accu = Builder.CreateAdd(Accu, NewBit);
                    // Updating Exponent
                    Pow = Builder.CreateMul(Pow, IRBase);
                    // Updating Mask
                    Mask = Builder.CreateShl(Mask, 1);
                }

                return Accu;
            }

            Value *rewriteAsBase2(Value *Operand, unsigned Base, Type *OriginalType, IRBuilder<> &Builder) {
                Type *ObfuscatedType = Operand->getType();

                const unsigned OriginalNbBit = OriginalType->getIntegerBitWidth();

                APInt APBase(ObfuscatedType->getIntegerBitWidth(), Base);

                // Initializing variables
                Value *R = Operand,
                      *IRBase = ConstantInt::get(ObfuscatedType, Base),
                      *IR2 = ConstantInt::get(ObfuscatedType, 2),
                      *Accu = ConstantInt::get(ObfuscatedType, 0);

                // Computing APInt max operand in case we need more than 64 bits
                Value *Pow = ConstantInt::get(ObfuscatedType, APIntPow(APBase, OriginalNbBit - 1));

                // Euclide Algorithm
                for(unsigned Bit = OriginalNbBit; Bit > 0; --Bit) {
                    // dbgs() << *Pow << "\t" << Bit - 1 << "\n"; 
                    // Updating NewValue
                    Value *Q = Builder.CreateUDiv(R, Pow);
                    Q = Builder.CreateURem(Q, IR2);
                    Value *ShiftedBit = Builder.CreateShl(Q, Bit - 1);
                    Accu = Builder.CreateOr(Accu, ShiftedBit);
                    R = Builder.CreateURem(R, Pow);
                    // Updating Exponent
                    Pow = Builder.CreateUDiv(Pow, IRBase);
                }
                // Cast back to original type
                return Builder.CreateZExtOrTrunc(Accu, OriginalType);
            }

            // Must return unsigned int > 2
            unsigned chooseBase(unsigned OriginalNbBit) {
                std::uniform_int_distribution<unsigned> Rand(3, maxBase(OriginalNbBit));
                unsigned Base = Rand(Generator);
                // dbgs() << "Chosen base " <<  Base << " in " << OriginalNbBit << " -> " << maxBase(OriginalNbBit) << "\n";
                return Base;
            }

            // Returns the max supported base for the given OriginalNbBit
            // 31 is the max base to avoid overflow 2**31 in requiredBits
            unsigned maxBase(unsigned OriginalNbBit) {
                return std::min((unsigned)std::pow(2, MaxSupportedSize / OriginalNbBit), (unsigned)(sizeof(unsigned) - 1));
            }

            // numbers of bits required to store the original type in the new base
            // Can hold up to twice the max of the original type to store the max result of the add
            // returns 0 if more than 128 bits are needed
            unsigned requiredBits(unsigned OriginalSize, unsigned targetBase) const {
                unsigned ret = (unsigned)(std::floor(OriginalSize * std::log2(targetBase) + 1));
                return ret <= MaxSupportedSize ? ret : 0;
            }

            // Builds the APInt exponent value at runtime
            // Required if the exponent value overflows uint64_t
            static APInt APIntPow(APInt const& Base, size_t Exponent) {
                APInt Accu(Base.getBitWidth(), 1u);
                for(; Exponent != 0; --Exponent)
                    Accu *= Base;
                return Accu;
            }
    };
}

char X_OR::ID = 0;
static RegisterPass<X_OR> X("X_OR", "Obfuscates XOR", false, false);

// register pass for clang use
static void registerX_ORPass(const PassManagerBuilder &,
        PassManagerBase &PM) {
    PM.add(new X_OR());
}
static RegisterStandardPasses
RegisterMBAPass(PassManagerBuilder::EP_EarlyAsPossible, registerX_ORPass);
