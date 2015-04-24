#ifndef __PROPAGATED_TRANSFORMATION_HPP__
#define __PROPAGATED_TRANSFORMATION_HPP__

#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ErrorOr.h"

#include <map>
#include <set>
#include <list>
#include <tuple>
#include <vector>
#include <unordered_map>

using namespace llvm;

struct Tree_t : public std::unordered_map<Instruction *, std::set<Instruction *>> {

    mapped_type roots() const {
        mapped_type Roots;
        std::transform(begin(),
                       end(),
                       std::inserter(Roots, Roots.begin()),
                       [this](Tree_t::value_type const &It) { return It.first; });

        for (auto const &Node : (*this))
            std::for_each(Node.second.cbegin(),
                          Node.second.cend(),
                          [&Roots](Tree_t::mapped_type::key_type const &Successors){Roots.erase(Successors);});
        return Roots;
    }
};

namespace PropagatedTransformation {
class PropagatedTransformation {
  protected:
    std::default_random_engine Generator;

    std::list<Tree_t> Forest;
    std::unordered_map<Instruction *, std::reference_wrapper<Tree_t>> TreeMap;

    std::map<std::pair<Value *, unsigned>, std::vector<Value *>> TransfoRegister;

    unsigned SizeParam;

    // Pure virtual members
    virtual BinaryOperator *isEligibleInstruction(Instruction *Inst) const = 0;
    // Should return an empty vector if sthg went wrong
    virtual std::vector<Value *> transformOperand(Value *Operand,
                                                  IRBuilder<> &Builder) = 0;
    virtual Value *transformBackOperand(std::vector<Value *> const &Operands,
                                        IRBuilder<> &Builder) = 0;
    virtual std::vector<Value *>
    applyNewOperation(std::vector<Value *> const &Operand1,
                      std::vector<Value *> const &Operand2,
                      Instruction *OriginalInstruction,
                      IRBuilder<> &Builder) = 0;

    // Implemented members
    void populateForest(BasicBlock &BB) {
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
    }


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

    void replaceUses(Value *OriginalValue, Value *NewValue, Tree_t const &T) {
        for (auto const &NVUse : OriginalValue->uses()) {
            Instruction *UseInst = dyn_cast<Instruction>(NVUse.getUser());
            if (not UseInst or T.find(UseInst) != T.cend())
                continue;
            for (unsigned I = 0; I < UseInst->getNumOperands(); ++I) {
                if (UseInst->getOperand(I) == OriginalValue) {
                    UseInst->setOperand(I, NewValue);
                }
            }
        }
    }

    // Checking if we've already transformed the operand or transform it
    ErrorOr<std::vector<Value *> const &> findOrTransformOperand(Value *Operand,
                                                       IRBuilder<> &Builder) {
        auto Pos = TransfoRegister.find(std::make_pair(Operand, SizeParam));
        if (Pos == TransfoRegister.end()) {
            std::vector<Value *> NewOperands =
                transformOperand(Operand, Builder);
            if (NewOperands.empty()) {
                dbgs() << "Obfuscation failed\n";
                return {std::errc::operation_not_supported};
            } else {
                TransfoRegister.emplace(std::make_pair(Operand, SizeParam),
                                        std::move(NewOperands));
                return {TransfoRegister.at(std::make_pair(Operand, SizeParam))};
            }
        } else
            return Pos->second;
    }

    ErrorOr<std::vector<Value *> const &>
    RecursiveTransform(Instruction *Inst, Tree_t const &T,
                       BasicBlock const &CurrentBB) {
        assert(Inst && "Invalid instruction.");
        IRBuilder<> Builder(Inst);

        Value *Operand1 = Inst->getOperand(0), *Operand2 = Inst->getOperand(1);

        ErrorOr<const std::vector<Value *>&> NewOperands1{std::errc::operation_not_supported},
                                             NewOperands2{std::errc::operation_not_supported};

        auto const &Successors = T.at(Inst);

        Instruction *IOperand1 = dyn_cast<Instruction>(Operand1),
                    *IOperand2 = dyn_cast<Instruction>(Operand2);

        // If Operand1 is not a node (i.e not a xor)
        if (not IOperand1 or IOperand1->getParent() != &CurrentBB or
            Successors.find(IOperand1) == Successors.cend())
            NewOperands1 = findOrTransformOperand(Operand1, Builder);
        else
            NewOperands1 = RecursiveTransform(IOperand1, T, CurrentBB);

        // Idem for Operand2
        if (not IOperand2 or IOperand2->getParent() != &CurrentBB or
            Successors.find(IOperand2) == Successors.cend())
            NewOperands2 = findOrTransformOperand(Operand2, Builder);
        else
            NewOperands2 = RecursiveTransform(IOperand2, T, CurrentBB);

        if (not NewOperands1 or not NewOperands2)
            return {std::errc::operation_not_supported};

        auto NewValues = applyNewOperation(NewOperands1.get(), NewOperands2.get(), Inst, Builder);

        if (NewValues.empty())
            return {std::errc::operation_not_supported};

        // Preparing the result in base 2 for later use
        // Should be optimized out if we don't use it.
        Value *InvertResult = transformBackOperand(NewValues, Builder);

        if (not InvertResult)
            return {std::errc::operation_not_supported};

        TransfoRegister.emplace(std::make_pair(Inst, SizeParam),
                                std::move(NewValues));

        replaceUses(Inst, InvertResult, T);

        return TransfoRegister.at(std::make_pair(Inst, SizeParam));
    }
};
}

#endif
