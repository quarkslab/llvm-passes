#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#ifndef NDEBUG
#include "llvm/IR/Verifier.h"
#endif
#include "llvm/Support/Debug.h"

#include <numeric>
#include <tuple>
#include <map>
#include <cmath>
#include <algorithm>

#include "../PropagatedTransformation/PropagatedTransformation.hpp"

using namespace llvm;

namespace {
class X_OR : protected PropagatedTransformation::PropagatedTransformation,
             public BasicBlockPass {

    typedef std::map<std::pair<unsigned, unsigned>, std::map<unsigned, APInt>>
        ExponentMaps_t;
    ExponentMaps_t ExponentMaps;

    Type *OriginalType;

  public:
    static char ID;

    X_OR() : BasicBlockPass(ID) {}

    virtual bool runOnBasicBlock(BasicBlock &BB) {
        bool modified = false;

        populateForest(BB);

        for (auto const &T : Forest) {
            auto Roots = T.roots();
            // Choosing NewBase
            SizeParam = chooseTreeBase(T, Roots);
            // If there was no valid base available:
            if (SizeParam < 3) {
                dbgs() << "X-OR: Couldn't pick base.\n";
                continue;
            }

            OriginalType = T.begin()->first->getType();

            for (auto Root : Roots) {
                if (RecursiveTransform(Root, T, BB))
                    modified = true;
                else {
                    dbgs() << "X_OR: Obfuscation failed.\n";
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
    // FIXME: capping at 128 bits because of APInt multiplication bug:
    // https://llvm.org/bugs/show_bug.cgi?id=19797
    const unsigned MaxSupportedSize = 128;
    std::default_random_engine Generator;

    std::map<std::pair<unsigned, unsigned>, std::map<unsigned, APInt>>
        ExponentMap;

    BinaryOperator *isEligibleInstruction(Instruction *Inst) const override {
        BinaryOperator *Op = dyn_cast<BinaryOperator>(Inst);
        if (not Op)
            return nullptr;
        if (Op->getOpcode() == Instruction::BinaryOps::Xor)
            return Op;
        return nullptr;
    }

    std::vector<Value *>
    applyNewOperation(std::vector<Value *> const &Operands1,
                      std::vector<Value *> const &Operands2, Instruction *,
                      IRBuilder<> &Builder) override {
        assert(not Operands1.empty() and not Operands2.empty());

        return std::vector<Value *>{
            Builder.CreateAdd(Operands1[0], Operands2[0])};
    }

    std::vector<Value *> transformOperand(Value *Operand,
                                          IRBuilder<> &Builder) override {
        if (!Operand->getType()->isIntegerTy())
            return std::vector<Value *>();

        const unsigned OriginalNbBit = Operand->getType()->getIntegerBitWidth(),
                       Base = SizeParam,
                       NewNbBit = requiredBits(OriginalNbBit, Base);

        if (not NewNbBit) {
            return std::vector<Value *>();
        }

        Type *NewBaseType = IntegerType::get(Operand->getContext(), NewNbBit);

        auto const &ExpoMap = getExponentMap(Base, OriginalNbBit, NewBaseType);

        // Initializing variables
        Value *Accu = Constant::getNullValue(NewBaseType),
              *InitMask = ConstantInt::get(NewBaseType, 1u);

        // Extending the original value to NewNbBit for bitwise and
        Value *ExtendedOperand = Builder.CreateZExt(Operand, NewBaseType);

        auto Range = getShuffledRange(OriginalNbBit);

        for (auto Bit : Range) {
            Value *Mask = Builder.CreateShl(InitMask, Bit);
            Value *MaskedNewValue = Builder.CreateAnd(ExtendedOperand, Mask);
            Value *BitValue = Builder.CreateLShr(MaskedNewValue, Bit);
            Value *Expo = ConstantInt::get(NewBaseType, ExpoMap.at(Bit));
            Value *NewBit = Builder.CreateMul(BitValue, Expo);
            Accu = Builder.CreateAdd(Accu, NewBit);
        }
        return std::vector<Value *>{Accu};
    }

    Value *transformBackOperand(std::vector<Value *> const &Operands,
                                IRBuilder<> &Builder) override {
        assert(Operands.size() && "No instructions provided.");
        Value *Operand = Operands[0];

        Type *ObfuscatedType = Operand->getType();

        const unsigned OriginalNbBit = OriginalType->getIntegerBitWidth(),
                       Base = SizeParam;

        // Initializing variables
        Value *IR2 = ConstantInt::get(ObfuscatedType, 2u),
              *IRBase = ConstantInt::get(ObfuscatedType, Base),
              *Accu = Constant::getNullValue(ObfuscatedType);

        auto const &ExpoMap =
            getExponentMap(Base, OriginalNbBit, ObfuscatedType);

        auto Range = getShuffledRange(OriginalNbBit);

        for (auto Bit : Range) {
            Value *Pow = ConstantInt::get(ObfuscatedType, ExpoMap.at(Bit));
            Value *Q = Builder.CreateUDiv(Operand, Pow);
            Q = Builder.CreateURem(Q, IRBase);
            Q = Builder.CreateURem(Q, IR2);
            Value *ShiftedBit = Builder.CreateShl(Q, Bit);
            Accu = Builder.CreateOr(Accu, ShiftedBit);
        }
        // Cast back to original type
        return Builder.CreateTrunc(Accu, OriginalType);
    }

    unsigned chooseTreeBase(Tree_t const &T, Tree_t::mapped_type const &Roots) {
        assert(T.size() && "Can't process an empty tree.");
        unsigned Max = maxBase(
                     T.begin()->first->getType()->getIntegerBitWidth()),
                 MinEligibleBase = 0;

        // Computing minimum base
        // Each node of the tree has a base equal to the sum of its two
        // successors' min base
        std::map<Value *, unsigned> NodeBaseMap;
        for (auto const &Root : Roots)
            MinEligibleBase = std::max(minimalBase(Root, T, NodeBaseMap), MinEligibleBase);

        ++MinEligibleBase;
        if (MinEligibleBase < 3 or MinEligibleBase > Max)
            return 0;
        std::uniform_int_distribution<unsigned> Rand(MinEligibleBase, Max);
        return Rand(Generator);
    }

    unsigned minimalBase(Value *Node, Tree_t const &T,
                         std::map<Value *, unsigned> &NodeBaseMap) {
        // Emplace new value and check if already passed this node
        if (NodeBaseMap[Node] != 0)
            return NodeBaseMap.at(Node);
        Instruction *Inst = dyn_cast<Instruction>(Node);
        // We reached a leaf
        if (not Inst or T.find(Inst) == T.end()) {
            NodeBaseMap.at(Node) = 1;
            return 1;
        } else {
            // Recursively check operands
            unsigned sum = 0;
            for (auto const &Operand : Inst->operands()) {
                if (NodeBaseMap[Operand] == 0)
                    minimalBase(Operand, T, NodeBaseMap);
                sum += NodeBaseMap.at(Operand);
            }
            // Compute this node's min base
            NodeBaseMap[Node] = sum;
            return sum;
        }
    }

    // Returns the max supported base for the given OriginalNbBit
    // 31 is the max base to avoid overflow 2**sizeof(unsigned) in requiredBits
    unsigned maxBase(unsigned OriginalNbBit) {
        assert(OriginalNbBit && "Bisize must be > 1");
        const unsigned MaxSupportedBase = sizeof(unsigned) * 8 - 1;
        if (OriginalNbBit >= MaxSupportedSize)
            return 0;
        if (MaxSupportedSize / OriginalNbBit > MaxSupportedBase)
            return MaxSupportedBase;
        return unsigned(2) << ((MaxSupportedSize / OriginalNbBit) - 1);
    }

    // numbers of bits required to store the original type in the new base
    // Can hold up to twice the max of the original type to store the max result
    // of the add
    // returns 0 if more than 128 bits are needed
    unsigned requiredBits(unsigned OriginalSize, unsigned TargetBase) const {
        assert(OriginalSize);
        if (TargetBase <= 2 or OriginalSize >= MaxSupportedSize)
            return 0;
        // 'Exact' formula : std::ceil(std::log2(std::pow(TargetBase,
        // OriginalSize) - 1));
        unsigned ret =
            (unsigned)std::ceil(OriginalSize * std::log2(TargetBase));
        // Need to make sure that the base can be represented too...
        // (For instance for 2 chained boolean xor)
        ret = std::max(ret, (unsigned)std::floor(std::log2(TargetBase)) + 1);
        return ret <= MaxSupportedSize ? ret : 0;
    }

    ExponentMaps_t::mapped_type
    getExponentMap(unsigned Base, unsigned OriginalNbBit, const Type *Ty) {
        // Eplacing if the pair doesn't exist, else return an iter to the
        // existing
        auto Position = ExponentMaps.emplace(
            std::make_pair(Base, OriginalNbBit), std::map<unsigned, APInt>());
        // If the map has not been computed yet
        if (Position.second) {
            unsigned NewNbBit = Ty->getIntegerBitWidth();
            APInt Pow(NewNbBit, 1u), APBase(NewNbBit, Base);
            for (unsigned Bit = 0; Bit < OriginalNbBit; ++Bit) {
                ExponentMaps.at(Position.first->first).emplace(Bit, Pow);
                Pow *= APBase;
            }
        }
        return Position.first->second;
    }
};
}

char X_OR::ID = 0;
static RegisterPass<X_OR> X("X-OR", "Obfuscates XORs", false, false);

// register pass for clang use
static void registerX_ORPass(const PassManagerBuilder &, PassManagerBase &PM) {
    PM.add(new X_OR());
}
static RegisterStandardPasses
    RegisterX_ORPass(PassManagerBuilder::EP_EarlyAsPossible, registerX_ORPass);
