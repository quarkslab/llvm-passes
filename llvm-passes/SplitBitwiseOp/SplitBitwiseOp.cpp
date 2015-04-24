#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"

#include "llvm/IR/LegacyPassManager.h"
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
#include <map>
#include <cmath>
#include <algorithm>

#include "../PropagatedTransformation/PropagatedTransformation.hpp"

using namespace llvm;

namespace {

std::set<unsigned> integerFactors(unsigned BitSize) {
    if(BitSize < 2)
        return {};
    std::set<unsigned> Factors;
    unsigned MaxFactor = (unsigned)std::sqrt(BitSize);
    for(unsigned I = 1; I <= MaxFactor; ++I)
        if(BitSize % I == 0) {
            Factors.insert(I);
            Factors.insert(BitSize / I);
        }
    return Factors;
}

// PASS
class SplitBitwiseOp
    : protected PropagatedTransformation::PropagatedTransformation,
      public BasicBlockPass {

    Type *OriginalType;

  public:
    static char ID;

    SplitBitwiseOp() : BasicBlockPass(ID) {}

    virtual bool runOnBasicBlock(BasicBlock &BB) {
        bool modified = false;

        populateForest(BB);

        for (auto const &T : Forest) {
            const auto Roots = T.roots();
            // Choosing SizeParam
            SizeParam = chooseSplitSize(T);
            // If there was no valid Size available:
            if (SizeParam == 0) {
                dbgs() << "split_binop: Couldn't pick split size.\n";
                continue;
            }

            OriginalType = T.begin()->first->getType();

            for (Instruction* Root : Roots) {
                if (RecursiveTransform(Root, T, BB)) {
                    modified = true;
                }
                else {
                    dbgs() << "SplitBinOp: Obfuscation failed.\n";
                    break;
                }
            }
        }
#ifndef NDEBUG
        verifyFunction(*BB.getParent());
#endif
        return modified;
    }

  private:
    std::default_random_engine Generator;

    unsigned chooseSplitSize(Tree_t const &T) {
        unsigned OriginalSize =
            T.begin()->first->getType()->getIntegerBitWidth();

        std::set<unsigned> Factors = integerFactors(OriginalSize);

        if (Factors.empty())
            return 0;

        std::uniform_int_distribution<unsigned> Rand(0, Factors.size() - 1);
        auto Pos = Factors.cbegin();
        std::advance(Pos, Rand(Generator));
        return *Pos;
    }

    BinaryOperator *isEligibleInstruction(Instruction *Inst) const override {
        if(BinaryOperator *Op = dyn_cast<BinaryOperator>(Inst)) {
            const Instruction::BinaryOps OpCode = Op->getOpcode();
            if (OpCode == Instruction::BinaryOps::Xor or
                OpCode == Instruction::BinaryOps::And or
                OpCode == Instruction::BinaryOps::Or) {
                return Op;
            }
        }
        return nullptr;
    }

    std::vector<Value *>
    applyNewOperation(std::vector<Value *> const &Operands1,
                      std::vector<Value *> const &Operands2,
                      Instruction *OriginalInstruction,
                      IRBuilder<> &Builder) override {
        assert(not Operands1.empty() and not Operands2.empty() && "Empty operand vector.");
        assert(Operands1.size() == Operands2.size() && "Operand vectors must have the same size.");

        BinaryOperator *Op = cast<BinaryOperator>(OriginalInstruction);

        const unsigned NumberOperations = Operands1.size();

        Instruction::BinaryOps OpCode = Op->getOpcode();
        std::vector<Value *> NewResults(NumberOperations);

        auto Range = getShuffledRange(NumberOperations);

        for (auto I : Range)
            NewResults[I] =
                Builder.CreateBinOp(OpCode, Operands1[I], Operands2[I]);

        return NewResults;
    }

    std::vector<Value *> transformOperand(Value *Operand,
                                          IRBuilder<> &Builder) override {
        const unsigned OriginalNbBit = Operand->getType()->getIntegerBitWidth(),
                       SplitSize = SizeParam,
                       NumberNewOperands = OriginalNbBit / SplitSize;

        Type *NewType = IntegerType::get(Operand->getContext(), SplitSize);

        std::vector<Value *> NewOperands(NumberNewOperands);

        Value *InitMask = ConstantInt::get(Operand->getType(), -1);
        InitMask = Builder.CreateLShr(InitMask, OriginalNbBit - SplitSize);

        auto Range = getShuffledRange(NumberNewOperands);

        for (auto I : Range) {
            Value *Mask = Builder.CreateShl(InitMask, SplitSize * I);
            Value *MaskedNewValue = Builder.CreateAnd(Operand, Mask);
            Value *NewOperandValue =
                Builder.CreateLShr(MaskedNewValue, I * SplitSize);
            // Using NewOperands to keep the order of split operands
            NewOperands[I] = Builder.CreateTrunc(NewOperandValue, NewType);
        }
        return NewOperands;
    }

    Value *transformBackOperand(std::vector<Value *> const &Operands,
                                IRBuilder<> &Builder) override {
        assert(Operands.size() && "Empty operand vector.");
        const unsigned NumberOperands = Operands.size(), SplitSize = SizeParam;

        Value *Accu = Constant::getNullValue(OriginalType);

        auto Range = getShuffledRange(NumberOperands);

        for (auto I : Range) {
            Value *ExtendedOperand =
                Builder.CreateZExt(Operands[I], OriginalType);
            Value *ShiftedValue =
                Builder.CreateShl(ExtendedOperand, I * SplitSize);
            Accu = Builder.CreateOr(Accu, ShiftedValue);
        }
        return Accu;
    }
};
}

char SplitBitwiseOp::ID = 0;
static RegisterPass<SplitBitwiseOp> X("SplitBitwiseOp",
                                      "Splits bitwise operators", false, false);

// register pass for clang use
static void registerSplitBitwiseOpPass(const PassManagerBuilder &,
                                       PassManagerBase &PM) {
    PM.add(new SplitBitwiseOp());
}
static RegisterStandardPasses
    RegisterSplitBitwisePass(PassManagerBuilder::EP_EarlyAsPossible,
                    registerSplitBitwiseOpPass);
