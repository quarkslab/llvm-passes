#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#ifndef NDEBUG
#include "llvm/IR/Verifier.h"
#endif
#include "llvm/Support/Debug.h"

#include <cassert>
#include <set>
#include <list>
#include <numeric>
#include <tuple>
#include <map>
#include <unordered_map>
#include <cmath>
#include <algorithm>

using namespace llvm;

namespace {

class X_OR : public BasicBlockPass {

    const unsigned MaxSupportedSize = 128;
    std::default_random_engine Generator;

    typedef std::unordered_map<Instruction *, std::set<Instruction *>> Tree_t;

    std::list<Tree_t> Forest;
    std::unordered_map<Instruction *, std::reference_wrapper<Tree_t>> TreeMap;

    std::map<std::pair<Value *, unsigned>, Value *> TransfoRegister;
    typedef std::map<std::pair<unsigned, unsigned>, std::map<unsigned, APInt>>
        ExponentMaps_t;
    ExponentMaps_t ExponentMaps;

  public:
    static char ID;

    X_OR() : BasicBlockPass(ID) {}

    virtual bool runOnBasicBlock(BasicBlock &BB) {
        bool modified = false;

        TreeMap.clear();
        Forest.clear();
        TransfoRegister.clear();

        for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),
                                           end = BB.end();
             I != end; ++I) {
            Instruction *Inst = &*I;
            if (isEligibleInstruction(Inst)) {
                // Adding an empty tree to the Forest to pass it to walkInstructions
                // If a merge occurs an older tree will be removed this
                // means that there can't be any empty tree in the Torest
                Forest.emplace_back();
                walkInstructions(Forest.back(), Inst);
            }
        }

        for (auto &T : Forest) {
            auto Roots = getRoots(T);
            const unsigned NewBase = chooseTreeBase(T, Roots);
            // If there was no valid base available:
            if (NewBase < 3) {
                dbgs() << "X_OR: Couldn't pick base.\n";
                continue;
            }

            for (auto Root : Roots) {
                if (recursiveTransform(Root, T, NewBase, BB))
                    modified = true;
                else {
                    dbgs() << "X_OR: Obfiscation failed.\n";
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
    // Returns roots of given tree
    Tree_t::mapped_type getRoots(Tree_t &T) {
        Tree_t::mapped_type Roots;
        std::transform(
            T.begin(), T.end(), std::inserter(Roots, Roots.begin()),
            [&Roots](Tree_t::value_type const &It) { return It.first; });
        for (auto const &Node : T)
            for (auto const &Successors : Node.second)
                Roots.erase(Successors);
        return Roots;
    }

    Value *recursiveTransform(Instruction *Inst, Tree_t const &T,
                              unsigned NewBase, BasicBlock const &CurrentBB) {
        assert(Inst && "Invalid instruction.");
        IRBuilder<> Builder(Inst);

        Value *Operand1 = Inst->getOperand(0), *Operand2 = Inst->getOperand(1);

        Value *NewOperand1 = nullptr, *NewOperand2 = nullptr;

        auto const &Successors = T.at(Inst);

        Instruction *IOperand1 = dyn_cast<Instruction>(Operand1),
                    *IOperand2 = dyn_cast<Instruction>(Operand2);

        // If Operand1 is not a node (i.e not a xor)
        if (not IOperand1 or IOperand1->getParent() != &CurrentBB or
            Successors.find(IOperand1) == Successors.cend())
            NewOperand1 = findOrTransformOperand(Operand1, NewBase, Builder);
        else
            NewOperand1 = recursiveTransform(IOperand1, T, NewBase, CurrentBB);

        // Idem for Operand2
        if (not IOperand2 or IOperand2->getParent() != &CurrentBB or
            Successors.find(IOperand2) == Successors.cend())
            NewOperand2 = findOrTransformOperand(Operand2, NewBase, Builder);
        else
            NewOperand2 = recursiveTransform(IOperand2, T, NewBase, CurrentBB);

        if (not NewOperand1 or not NewOperand2)
            return nullptr;

        Value *NewValue = Builder.CreateAdd(NewOperand1, NewOperand2);

        if (not NewValue)
            return nullptr;

        TransfoRegister.emplace(std::make_pair(Inst, NewBase), NewValue);

        // Preparing the result in base 2 for later use
        // Should be optimized out if we don't use it.
        Value *InvertResult =
            transformToBaseTwoRepr(NewValue, NewBase, Inst->getType(), Builder);

        if (not InvertResult)
            return nullptr;

        for (auto const &NVUse : Inst->uses()) {
            Instruction *UseInst = dyn_cast<Instruction>(NVUse.getUser());
            if (not UseInst or T.find(UseInst) != T.cend())
                continue;

            for (unsigned I = 0; I < UseInst->getNumOperands(); ++I) {
                if (UseInst->getOperand(I) == (Value *)Inst) {
                    UseInst->setOperand(I, InvertResult);
                }
            }
        }

        return NewValue;
    }

    // Checking if we've already transformed the operand or transform it
    Value *findOrTransformOperand(Value *Operand, unsigned SizeParam,
                                  IRBuilder<> &Builder) {
        auto Pos = TransfoRegister.find(std::make_pair(Operand, SizeParam));
        if (Pos == TransfoRegister.end()) {
            Value *NewOperand = rewriteAsBaseN(Operand, SizeParam, Builder);
            if (not NewOperand) {
                dbgs() << "X_OR: Obfuscation failed.\n";
                return nullptr;
            }
            TransfoRegister.emplace(std::make_pair(Operand, SizeParam),
                                    NewOperand);
            return TransfoRegister.at(std::make_pair(Operand, SizeParam));
        }
        return Pos->second;
    }

    BinaryOperator *isEligibleInstruction(Instruction *Inst) {
        BinaryOperator *Op = dyn_cast<BinaryOperator>(Inst);
        if (Op and Op->getOpcode() == Instruction::BinaryOps::Xor)
            return Op;
        return nullptr;
    }

    // Building the adjacency list representing the dependencies
    // between XORs
    void walkInstructions(Tree_t &T, Instruction *Inst) {
        if (not isEligibleInstruction(Inst))
            return;
        // Look for the Inst in the TreeMap
        auto Pos = TreeMap.find(Inst);
        if (Pos == TreeMap.end()) {
            T.emplace(Inst, Tree_t::mapped_type());
            for (auto const &Op : Inst->operands()) {
                Instruction *OperandInst = dyn_cast<Instruction>(&Op);
                if (OperandInst and isEligibleInstruction(OperandInst))
                    T.at(Inst).insert(OperandInst);
            }
            TreeMap.emplace(Inst, T);
            for (auto const &NVUse : Inst->uses()) {
                Instruction *UseInst = dyn_cast<Instruction>(NVUse.getUser());
                if (not UseInst)
                    continue;
                walkInstructions(T, UseInst);
            }
        } else {
            Tree_t &OlderTree = Pos->second.get();
            if (&OlderTree != &T) {
                // Merge trees
                for (auto It : OlderTree) {
                    T[It.first].insert(OlderTree[It.first].begin(),
                                       OlderTree[It.first].end());
                    // Redirecting TreeMap refs
                    TreeMap.at(It.first) = T;
                }
                Forest.remove_if(
                    [&OlderTree](Tree_t const &T) { return &T == &OlderTree; });
            }
        }
    }

    std::vector<unsigned> getShuffledRange(unsigned UpTo) {
        std::vector<unsigned> Range(UpTo);
        std::iota(Range.begin(), Range.end(), 0u);
        std::random_shuffle(Range.begin(), Range.end());
        return Range;
    }

    Value *rewriteAsBaseN(Value *Operand, unsigned Base, IRBuilder<> &Builder) {
        const unsigned OriginalNbBit = Operand->getType()->getIntegerBitWidth(),
                       NewNbBit = requiredBits(OriginalNbBit, Base);
        if (not NewNbBit) {
            return nullptr;
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

        return Accu;
    }

    Value *transformToBaseTwoRepr(Value *Operand, unsigned Base, Type *OriginalType,
                          IRBuilder<> &Builder) {
        Type *ObfuscatedType = Operand->getType();

        const unsigned OriginalNbBit = OriginalType->getIntegerBitWidth();

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
        return Builder.CreateZExtOrTrunc(Accu, OriginalType);
    }

    unsigned chooseTreeBase(Tree_t const &T, Tree_t::mapped_type const &Roots) {
        assert(T.size() && "Can't process an empty tree.");
        unsigned Max = maxBase(
                     T.begin()->first->getType()->getIntegerBitWidth()),
                 MinEligibleBase = 0;

        // Computing minimum base
        // Each node of the tree has a base equal to the sum of its two
        // successors' mins
        std::map<Value *, unsigned> NodeBaseMap;
        for (auto const &Root : Roots)
            MinEligibleBase = std::max(recursiveTreeClimb(Root, T, NodeBaseMap), MinEligibleBase);

        ++MinEligibleBase;
        if (MinEligibleBase < 3 or MinEligibleBase > Max)
            return 0;
        std::uniform_int_distribution<unsigned> Rand(MinEligibleBase, Max);
        return Rand(Generator);
    }

    unsigned recursiveTreeClimb(Value *Node, Tree_t const &T,
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
                    recursiveTreeClimb(Operand, T, NodeBaseMap);
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
        assert(OriginalNbBit && "Number of bits must be > 1.");
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
        assert(OriginalSize && "Number of bits must be > 1.");
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
static RegisterPass<X_OR> X("X_OR", "Obfuscates XOR", false, false);

// register pass for clang use
static void registerX_ORPass(const PassManagerBuilder &, PassManagerBase &PM) {
    PM.add(new X_OR());
}
static RegisterStandardPasses
    RegisterMBAPass(PassManagerBuilder::EP_EarlyAsPossible, registerX_ORPass);
