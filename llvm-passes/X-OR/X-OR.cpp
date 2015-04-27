#include "llvm/Pass.h"
#include "llvm/IR/Constants.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#ifndef NDEBUG
#include "llvm/IR/Verifier.h"
#endif
#include "llvm/Support/Debug.h"


using namespace llvm;

namespace {
class X_OR : public BasicBlockPass {
  public:
    static char ID;

    X_OR() : BasicBlockPass(ID) {}

    virtual bool runOnBasicBlock(BasicBlock &BB) {
        bool modified = false;

        for (auto I = BB.getFirstInsertionPt(), end = BB.end(); I != end; ++I) {
            Instruction &Inst = *I;
            // Checking Instruction eligibility
            if (not isEligibleInstruction(Inst))
                continue;

            IRBuilder<> Builder(&Inst);

            const unsigned NewBase = chooseBase(
                Inst.getType()->getIntegerBitWidth());

            // Rewritting operands in new base
            Value *NewOperand1 =
                      rewriteAsBaseN(Inst.getOperand(0), NewBase, Builder),
                  *NewOperand2 =
                      rewriteAsBaseN(Inst.getOperand(1), NewBase, Builder);

            // If sthg went wrong abort
            if (not NewOperand1 or not NewOperand2) {
                dbgs() << "X-OR: xor obfuscation failed\n";
                continue;
            }

            Value *NewValue = Builder.CreateAdd(NewOperand1, NewOperand2);

            // Preparing the result in base 2 for later use
            Value *InvertResult =
                transformToBaseTwoRepr(NewValue, NewBase, Inst.getType(), Builder);

            // Casting back for non XOR operation on NewValue
            Inst.replaceAllUsesWith(InvertResult);

            modified = true;
        }
#ifndef NDEBUG
        verifyFunction(*BB.getParent());
#endif
        return modified;
    }

  private:
    static constexpr unsigned MaxSupportedSize = 128;
    std::default_random_engine Generator;

    BinaryOperator *isEligibleInstruction(Instruction &Inst) {
        if(BinaryOperator *Op = dyn_cast<BinaryOperator>(&Inst))
          if (Op->getOpcode() == Instruction::BinaryOps::Xor)
            return Op;
        return nullptr;
    }

    Value *rewriteAsBaseN(Value *Operand, unsigned Base, IRBuilder<> &Builder) {
        const unsigned OriginalNbBit = Operand->getType()->getIntegerBitWidth(),
                       NewNbBit = requiredBits(OriginalNbBit, Base);
        if (!NewNbBit)
            return nullptr;

        Type *NewBaseType = IntegerType::get(Operand->getContext(), NewNbBit);

        Constant *IRBase = ConstantInt::get(NewBaseType, Base);
        // Initializing variables
        Value *Accu = ConstantInt::getNullValue(NewBaseType),
              *Mask = ConstantInt::get(NewBaseType, 1),
              *Pow = ConstantInt::get(NewBaseType, 1);

        // Extending the original value to NewNbBit for bitwise and
        Value *ExtendedOperand = Builder.CreateZExt(Operand, NewBaseType);

        for (unsigned Bit = 0; Bit < OriginalNbBit; ++Bit) {
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

    Value *transformToBaseTwoRepr(Value *Operand, unsigned Base, Type *OriginalType,
                          IRBuilder<> &Builder) {
        Type *ObfuscatedType = Operand->getType();

        const unsigned OriginalNbBit = OriginalType->getIntegerBitWidth();

        APInt APBase(ObfuscatedType->getIntegerBitWidth(), Base);

        // Initializing variables
        Value *R = Operand, *IRBase = ConstantInt::get(ObfuscatedType, Base),
              *IR2 = ConstantInt::get(ObfuscatedType, 2),
              *Accu = ConstantInt::getNullValue(ObfuscatedType);

        // Computing APInt max operand in case we need more than 64 bits
        Value *Pow = ConstantInt::get(ObfuscatedType,
                                      APIntPow(APBase, OriginalNbBit - 1));

        // Euclide Algorithm
        for (unsigned Bit = OriginalNbBit; Bit > 0; --Bit) {
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
        return Rand(Generator);
    }

    // Returns the max supported base for the given OriginalNbBit
    // 31 is the max base to avoid overflow 2**sizeof(unsigned) in requiredBits
    unsigned maxBase(unsigned OriginalNbBit) {
        assert(OriginalNbBit);
        const unsigned MaxSupportedBase = sizeof(unsigned) * 8 - 1;
        if (OriginalNbBit >= MaxSupportedSize)
            return 0;
        if (MaxSupportedSize / OriginalNbBit > MaxSupportedBase)
            return MaxSupportedBase;
        return std::integral_constant<unsigned, 2>::value << ((MaxSupportedSize / OriginalNbBit) - 1);
    }

    // numbers of bits required to store the original type in the new base
    // returns 0 if more than MaxSupportedSize bits are needed
    unsigned requiredBits(unsigned OriginalSize, unsigned TargetBase) {
        assert(OriginalSize);
        if (TargetBase <= 2 or OriginalSize >= MaxSupportedSize)
            return 0;
        // 'Exact' formula : std::ceil(std::log2(std::pow(TargetBase,
        // OriginalSize) - 1));
        unsigned ret =
            (unsigned)std::ceil(OriginalSize * std::log2(TargetBase));

        // Need to make sure that the base can be represented too...
        // (For instance if the OriginalSize == 1 and TargetBase == 4)
        ret = std::max(ret, (unsigned)std::floor(std::log2(TargetBase)) + 1);
        return ret <= MaxSupportedSize ? ret : 0;
    }

    // Builds the APInt exponent value at runtime
    // Required if the exponent value overflows uint64_t
    static APInt APIntPow(APInt const &Base, size_t Exponent) {
        APInt Accu(Base.getBitWidth(), 1u);
        for (; Exponent != 0; --Exponent)
            Accu *= Base;
        return Accu;
    }
};
}

char X_OR::ID = 0;
static RegisterPass<X_OR> X("X_OR", "Obfuscates XOR", false, false);

// register pass for clang use
static void registerX_ORPass(const PassManagerBuilder &, PassManagerBase &PM) {
    PM.add(new X_OR());
}
static RegisterStandardPasses
    RegisterX_ORPass(PassManagerBuilder::EP_EarlyAsPossible, registerX_ORPass);
