==============================================
Turning Regular Code Into Atrocities With LLVM
==============================================

Objective
=========

The objective of this post is to explain the basics of LLVM bytecode obfuscation through an out-of-source build single simple pass.
*'But why obfuscate the LLVM bytecode? Why not the source code? Or the binary?'*, you may ask.

Well it's because LLVM is *super swag* right now and using it is cool.
But, regarding the engineering aspects, it is because there are lots of front-ends converting different languages into the same LLVM bytecode (Clang/Clang++ for C/C++, Mono LLVM for C#, Pyston for... Python and so on).
Hence by working at the bytecode level we can obfuscate programs written in many languages without even knowing them.
Another good thing is that the obfuscation can be easily integrated with the existing compilation chains: just add a few obfuscation flags.

Now let's talk about what we're going to do.
Our mission (and we have no choice but to accept it) is to obfuscate all null literals in the code.
It means that we are going to replace (almost) all the zeroes in the code by a non-trivial boolean expression, proved to be always false.

        prime1 * ((x | any1)**2) != prime2 * ((y | any2)**2)

Given that:

    - ``prime1`` and ``prime2`` are *distinct* prime numbers
    - ``any1`` and ``any2`` are *distinct* strictly positive random numbers
    - ``x`` and ``y`` are two variables picked from the program (they have to be reachable from the obfuscation instructions)

This expression will always return a boolean zero (false). The idea is to insert this test into our code, just before the 0 we want to obfuscate and to replace this 0 by the result of our comparison.
As you have probably noticed we will have to pay attention to the type of the original 0 and make sure we cast the result of our expression to its type.

This obfuscation may not be the most sophisticated ever written but it's enough to learn the basics of LLVM bytecode obfuscation and maybe to annoy our friends in reverse engineering for a few minutes... until they use a nicely crafted `miasm <https://code.google.com/p/miasm/>`_ script!

Requirements
============

Programming background
**********************
To go through this tutorial you only need to be able to read C/C++ code. We will learn the basics of LLVM API and LLVM bytecode together.

Environment
***********
If you want to experiment along with this tutorial (which is strongly recommended) you will need to set up an LLVM development environment.

    * First let's download the LLVM 3.5 sources::

        >$ git clone --depth=1 --branch=release_35 https://github.com/llvm-mirror/llvm path/to/llvm/sources

    * Now download the clang sources inside the llvm_src/tools directory::

        >$ cd path/to/llvm/sources/tools && git clone --depth=1 --branch=release_35 https://github.com/llvm-mirror/clang

    * Create a build directory somewhere *out of the llvm source tree*::

        >$ mkdir path/to/llvm/build

    * Let's build LLVM::

        >$ cd path/to/llvm/build && cmake path/to/llvm/sources
        >$ make -j

    * And... wait...::

        >IRL sleep 1000

    * (Optional) You should set your path to include the freshly baked LLVM tools and clang::

        >$ export PATH=path/to/llvm/build/bin:$PATH

    * (Optional) You can run the test suite to make sure that LLVM was built correctly::

        >$ make check

    * (Optional) If you want to run the existing tests you first need to install `pip <https://pypi.python.org/pypi/pip>`_ (python-pip). Then use it to install lit::

        >$ pip install lit


You have just built LLVM and clang but we are going to build the passes out of the LLVM source tree. To do so we have prepared a git repository with the basic infrastructure::

        >$ git clone https://github.com/quarkslab/llvm-passes

From now on we will be working exclusively inside the ``llvm-passes`` folder (we will refer to it as ``$PASSDIR``). So let's visit our new office:

    * *cmake*: cmake definitions to check the Python environment. Required to generate our passes test suites.
    * *doc*: contains the sources of this tutorial, in case you find a shaming typo.
    * *llvm-passes*: contains one subdirectory per pass, and a ``CMakeList.txt`` used to generate the passes.
    * *tests*: tests and validation for our passes, contains one directory per pass. The tests are using llvm-lit, the LLVM integrated validation tool.
    * *CMakeList.txt*: the file used to generate the required Makefiles


Let's obfuscate!
================

Now that the environment is ready we will start writing the obfuscating pass. You may have noticed that there already is an ``ObfuscateZero`` dir in ``$PASSDIR/llvm-passes``.
This is the pass we are going to reproduce step by step. So unless you want to get spoiled don't look at it yet.

Now we have to deal with the hardest part of LLVM pass development (and software development in general), namely finding a name for our project.
Since I am not really inspired and ``ObfuscateZero`` is already taken, let's call our new pass *MyPass*.

We need a new directory for our pass::

    >$ mkdir $PASSDIR/llvm-passes/MyPass

And we will write the pass in ``$PASSDIR/llvm-passes/MyPass/MyPass.cpp``.


One with nothing
****************

The minimal compiling code for an LLVM pass is the following. It is explained `there <http://llvm.org/docs/WritingAnLLVMPass.html#basic-code-required>`_ so I won't explain it again and focus on the obfuscation part.

.. code:: C++

    #include "llvm/Pass.h"
    #include "llvm/IR/Function.h"
    #include "llvm/Support/raw_ostream.h"

    #include "llvm/IR/LegacyPassManager.h"
    #include "llvm/Transforms/IPO/PassManagerBuilder.h"

    using namespace llvm;

    namespace {
    class MyPass : public BasicBlockPass {
    public:
      static char ID;

      MyPass() : BasicBlockPass(ID) {}

      bool runOnBasicBlock(BasicBlock &BB) override {
        errs() << "I m running on a block...\n";
        return false;
      }

    };
    }

    char MyPass::ID = 0;
    static RegisterPass<MyPass> X("MyPass", "Obfuscates zeroes",
                                         false, false);

    // register pass for clang use
    static void registerMyPassPass(const PassManagerBuilder &,
                                   PassManagerBase &PM) {
      PM.add(new MyPass());
    }
    static RegisterStandardPasses
        RegisterMBAPass(PassManagerBuilder::EP_EarlyAsPossible,
                        registerMyPassPass);

If you have been paying attention so far you should remember that we are going to obfuscate null literals.
And to do so we will randomly pick two variables reachable from where the replacement occurs.
So, in order to keep the pass as simple as possible we are going to work at the basic bloc level, this way there will be no reachability problems with the variables we encounter.
This is why our class derives from the ``BasicBlockPass`` class.

This could be greatly enhanced using `dominators <http://llvm.org/docs/doxygen/html/classllvm_1_1DominatorTree.html>`_ and a scan for Module scope variables, but that's... another story!

.. code:: C++

    class MyPass : public BasicBlockPass


Do or do not there is no... test
********************************

I am sure that your are eager to compile and run this empty pass. Thanks to the files provided in the `git repo you've just cloned <https://github.com/quarkslab/llvm-passes>`_ it's actually quite easy.
First you need to tell cmake that your pass should be compiled by adding it in the file ``$PASSDIR/llvm-passes/CMakeList.txt``.
It should now look like this:

.. code:: cmake

    set(EPONA_LLVM_MODULES
        ObfuscateZero
        MyPass
    )

Now we are going to build the pass:

.. code:: bash

    >$ cd $PASSDIR
    >$ mkdir build
    >$ cd build
    >$ cmake -DLLVM_ROOT=path/to/your/llvm/build ..
    >$ make

And now let's run our pass with clang. We need a test file, write the following code somewhere:

.. code:: c

    #include <stdio.h>

    int foo(){return 1;}

    int main() {
        puts("Hello world");

        return 0;
    }

You can turn it into LLVM bytecode using:

.. code:: bash

    >$ clang -S -emit-llvm path/to/test/file.c -o file.ll

Or compile it with our awesome pass using:

.. code:: bash

    >$ clang -Xclang -load -Xclang $PASSDIR/build/llvm-passes/LLVMMyPass.so path/to/test/file.c -o awesome.out

Or if you just want to process the LLVM bytecode file:

.. code:: bash

  >$ opt -S -load $PASSDIR/build/llvm-passes/LLVMMyPass.so -MyPass path/to/test/file.ll -S -o out.ll

You can also generate the modified LLVM bytecode in a single call:

.. code:: bash

    >$ clang -S -emit-llvm -Xclang -load -Xclang $PASSDIR/build/llvm-passes/LLVMMyPass.so path/to/test/file

Since there are two basic blocks in our code (one in each function, ``foo`` and ``main``), we see the message "I m running on a block..." twice!

Congratulations you have compiled your first program with an LLVM pass! (You can test the executable, it should work... shouldn't it?)

Playtime is over
****************

The method we have to implement is ``runOnBasicBlock`` which takes as parameter a reference to the current block. Let's proceed step by step.

Finding null literals
+++++++++++++++++++++

To find the null literals we need to iterate over every instruction of the block and check if one of the operands is null.

.. code:: C++

  //Add the following to your headers
  #include "llvm/IR/Constants.h"
  #include "llvm/IR/Instructions.h"

  //Add the following to MyPass
  bool runOnBasicBlock(BasicBlock &BB) override {
    // Not iterating from the beginning to avoid obfuscation of Phi instructions
    // parameters
    for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),
                                       end = BB.end();
         I != end; ++I) {
      Instruction &Inst = *I;
      // We are not using an iterator because we will need i later.
      for (size_t i = 0; i < Inst.getNumOperands(); ++i) {
        if (Constant *C = isValidCandidateOperand(Inst.getOperand(i))) {
          errs() << "I've found one sir!\n";
        }
      }
    }
    return false;
   }

  Constant *isValidCandidateOperand(Value *V) {
    Constant *C;
    if (!(C = dyn_cast<Constant>(V))) return nullptr;
    if (!C->isNullValue()) return nullptr;
    // We found a NULL constant, lets validate it
    if(!C->getType()->isIntegerTy()) {
      // dbgs() << "Ignoring non integer value\n";
      return nullptr;
    }
    return C;
  }

The ``runOnBasicBlock`` method is going to iterate through all the instructions of the block (``for`` loop) and check if any operand of those instructions is an eligible null literal.
If any of the operand is a null literal we print a message on the debug stream and we continue.
You may have noticed the for loop is initialized with ``BB.getFirstInsertionPt()``.
We could have iterated through the block with a foreach like:

.. code:: C++

    for(auto &I : BB) {
    }

But we do not want to modify some of the special instructions located at the beginning of the block (the `phi instructions <http://en.wikipedia.org/wiki/Static_single_assignment_form#Converting_out_of_SSA_form>`_), so we skip them altogether and set the iterator to the first 'normal' instruction.

The ``isValidCandidateOperand`` method checks if its parameter is a literal (constant means literal in LLVM, not variable declared ``const``).
It also checks the type of the literal, it must not be a pointer or a floating point value (you will see later why).
The type checks are done with the ``dyn_cast<>`` function which checks if its parameter can be cast to the type given by the template parameter.
(``dyn_cast<>`` is used in LLVM instead of RTTI(run time type information) because it was deemed too `expensive <http://llvm.org/docs/CodingStandards.html#do-not-use-rtti-or-exceptions>`_.)
If all those conditions are satisfied and the literal is null we return a pointer to the operand (cast as a ``Constant``) else ``nullptr``.

If you compile and run the pass on our test code it finds **two** null literals when we just expected it to find the one from ``return 0``.

Let's take a look at the LLVM bytecode generated by clang:

.. code:: bash

    # The pass is not necessary now since it doesn't change anything, but it will be later.
    >$ clang++ -S -emit-llvm -Xclang -load -Xclang $PASSDIR/build/llvm-passes/LLVMMyPass.so path/to/test/file -o /tmp/awesome.ll

We get the following:

.. code:: llvm

    ; Function Attrs: nounwind uwtable
    define i32 @foo() #0 {
      ret i32 1
    }

    ; Function Attrs: nounwind uwtable
    define i32 @main() #0 {
      %1 = alloca i32, align 4 ; This instruction...
      store i32 0, i32* %1     ; ... and this one are useless, they would be deleted if we used an optimization flag.
      %2 = call i32 @puts(i8* getelementptr inbounds ([13 x i8]* @.str, i32 0, i32 0))
      ret i32 0
    }

The two 0 that triggered the debug message from our pass are in the ``store`` and ``ret`` instructions.
As you can see the lowering from C to LLVM bytecode produces a slightly more verbose code.
While debugging your future passes you will probably have to read a lot of bytecode so you should familiarize yourself with it.
Lucky for you it's pretty easy to read (at least compared to asm) and strongly typed (this helps a lot).


We've found your replacement
++++++++++++++++++++++++++++

Now that we can find null literals, we need to be able to replace them.
We need:

    1. To know the variables reachable from the instruction containing the eligible literal
    2. To generate the instructions of the arithmetic expression seen earlier
    3. To insert those expressions back into the code
    4. (Optional) Generate random prime numbers

Reachable variables
~~~~~~~~~~~~~~~~~~~

To be sure to have a pool of **reachable** variable during our obfuscation, we are going to register all the variables with integral type we come across while iterating through the block instructions.

We will slightly modify the code to:
    * add a class member vector storing pointers to the Integer/values of interest. We will empty it at the end of every block.
    * add a method to check the type of the instruction and store it in the vector if it is eligible.
    * call the above mentioned method from the main loop.

Our class becomes:

.. code:: C++

    //Add this to your includes
    #include <vector>


    class MyPass : public BasicBlockPass {
      std::vector<Value *> IntegerVect;

    public:

      static char ID;

      MyPass() : BasicBlockPass(ID) {}

      bool runOnBasicBlock(BasicBlock &BB) override {
        IntegerVect.clear();

        // Not iterating from the beginning to avoid obfuscation of Phi instructions
        // parameters
        for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),
                                           end = BB.end();
             I != end; ++I) {
          Instruction &Inst = *I;
            for (size_t i = 0; i < Inst.getNumOperands(); ++i) {
              if (Constant *C = isValidCandidateOperand(Inst.getOperand(i))) {
                errs() << "I've found one sir!\n";
              }
            }
            registerInteger(Inst);
        }
        return false;
      }

    private:
      void registerInteger(Value &V) {
        if (V.getType()->isIntegerTy()) {
          IntegerVect.push_back(&V);
          errs() << "Registering an integer!" << V << "\n";
        }
      }

      Constant *isValidCandidateOperand(Value *V) {
        Constant *C;
        if (!(C = dyn_cast<Constant>(V))) return nullptr;
        if (!C->isNullValue()) return nullptr;
        // We found a NULL constant, lets validate it
        if(!C->getType()->isIntegerTy()) {
          // dbgs() << "Ignoring non integer value\n";
          return nullptr;
        }
        return C;
      }
    };


and replace your test code by this updated version:

.. code:: c

    #include <stdio.h>

    int foo(){return 1;}

    int main() {
        int a = 2;
        puts("Hello world");
        a *= 3;

        return 0;
    }


If you run the pass on our new test file you'll notice that the pass finds **3** integers to register corresponding to %2, %3 and %4 in the following bytecode:

.. code:: llvm

    ; Function Attrs: nounwind uwtable
    define i32 @main() #0 {
      %1 = alloca i32, align 4
      %a = alloca i32, align 4
      store i32 0, i32* %1
      store i32 2, i32* %a, align 4
      %2 = call i32 @puts(i8* getelementptr inbounds ([13 x i8]* @.str, i32 0, i32 0))
      %3 = load i32* %a, align 4
      %4 = mul nsw i32 %3, 3
      store i32 %4, i32* %a, align 4
      ret i32 0
    }


There are a few things that you should remember from this little modification:
    * The LLVM bytecode is in `SSA form <http://en.wikipedia.org/wiki/Static_single_assignment_form>`_, so you will see variables that you didn't explicitly declared appear in the bytecode. Typically temporary result or ``loads``.
    * A variable declaration in your code returns a **pointer** in the bytecode not an instance of the type of the variable. This is because Clang translates variable declarations into variables allocated on the stack (through the ``alloca`` instruction). A later pass (Mem2reg) takes care of putting them in registers when possible.
    * You *need* to look at the bytecode to understand what you're *actually* telling LLVM to do (at least at first :p).
    * The return value of ``errs()`` is overloaded for most LLVM types, so use it! This is **very** useful for debug. (You can even use it on blocks, functions, ...)

I will make this entire pig disappear!
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ok we're almost done, the only thing left is to generate the new instructions and insert them into the code.
For those of you who forgot (or skipped the intro) we are going to replace the null integer literals by the result of the expression:

        prime1 * ((x | any1)**2) != prime2 * ((y | any2)**2)

Given that:

    - prime1 and prime2 are *distinct* prime numbers
    - any1 and any2 are *distinct* strictly positive random numbers
    - x and y are two variables picked from the program (they have to be reachable from the obfuscation instructions)

We will write a new method ``replaceZero`` that will do all the funny stuff. However given the size of the function we will detail it step by step:

First please add the following to your source file.

.. code:: C++

  // Insert with the other #include
  #include "llvm/IR/IRBuilder.h"
  #include <random>

  // Insert just before the MyClass declaration
  using prime_type = uint32_t;

Our ``replaceZero`` method will replace the null operand(s) of an instruction and return a pointer to the new operand(s) (or ``nullptr`` if a problem occurs). This gives us the following signature:

.. code:: C++

  Value* replaceZero(Instruction &Inst, Value* VReplace) {
    // Replacing 0 by:
    // prime1 * ((x | any1)^2) != prime2 * ((y | any2)^2)
    // with prime1 != prime2 and any1 != 0 and any2 != 0

To generate our new formula we need 2 distinct prime numbers:

.. code:: C++

    prime_type p1 = getPrime(),
               p2 = getPrime(p1);

    if(p2 == 0 || p1 == 0)
        return nullptr;

The LLVM bytecode is strongly typed so we will need to play a little with the types.
The important types are the type of the operand we are going to replace and the type in which we will do the operations of the obfuscation expression.
For the intermediary operations we will use the ``prime_type`` we've just declared (in this case ``uint32_t``).
However we need to be careful about type conversions and the type overflows (we will see later why and how).

.. code:: C++

    Type *ReplacedType = VReplace->getType(),
         *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),
                                              sizeof(prime_type) * 8);

Next we need to choose randomly two reachable variables (possibly twice the same) and two random strictly positive integers.
For the variables we are going to randomly pick values in ``IntegerVect``.

.. code:: C++

    // Abort the obfuscation if we have encontered no integers so far
    if (IntegerVect.empty()) {
      return nullptr;
    }

    // Random distribution to pick variables from IntegerVect
    std::uniform_int_distribution<size_t> Rand(0, IntegerVect.size() - 1);
    // Random distribution to pick Any1 and Any2 from [1, 10]
    std::uniform_int_distribution<size_t> RandAny(1, 10);

    // Indexes chosen for x and y
    size_t Index1 = Rand(Generator), Index2 = Rand(Generator);

If we overflow our intermediary type in one of the new instructions we could lose the property that the obfuscating comparison is always false.
We could replace a zero by... something else.
So we could change the result(s) produced by the code, and we want to avoid that all costs.
To prevent overflowing we have set the maximum for Any1 and Any2 to 10, but this is not enough.
We need to make sure that x and y are not too big. The trick is that we have no information on their value at compile time.
The solution we chose is to apply a bitmask to x and y in order to obtain a variable of which we know the max value.

The careful reader may have noticed that uniformly picking from ``IntegerVect`` is not truly uniform as we did not check for uniqueness of its elements ;-)

.. code:: C++

    // Creating the LLVM objects representing literals
    Constant *any1 = ConstantInt::get(IntermediaryType, 1 + RandAny(Generator)),
             *any2 = ConstantInt::get(IntermediaryType, 1 + RandAny(Generator)),
             *prime1 = ConstantInt::get(IntermediaryType, p1),
             *prime2 = ConstantInt::get(IntermediaryType, p2),
             // Bitmask to prevent overflow
             *OverflowMask = ConstantInt::get(IntermediaryType, 0x00000007);

Now that we have everything we need we will create our new instructions.
To insert new instructions **before** a specific instruction we use an ``IRBuilder``.
This object will create instructions and insert them before the instruction given to its constructor.
And we need to insert our new instructions before the instruction we are working on. That's why ``replaceZero`` takes an Instruction as parameter. We will forward it to the builder.

.. code:: C++

    IRBuilder<> Builder(&Inst);

    // lhs
    // Casting x to our intermediary type
    Value *LhsCast =
        Builder.CreateZExtOrTrunc(IntegerVect.at(Index1), IntermediaryType);
    // Registering the new integers for a future obfuscation
    registerInteger(*LhsCast);
    // To avoid overflow and truncate x
    Value *LhsAnd = Builder.CreateAnd(LhsCast, OverflowMask);
    registerInteger(*LhsAnd);
    // Creating LhsOr = (x | any1)
    Value *LhsOr = Builder.CreateOr(LhsAnd, any1);
    registerInteger(*LhsOr);
    // LhsOr * LhsOr
    Value *LhsSquare = Builder.CreateMul(LhsOr, LhsOr);
    registerInteger(*LhsSquare);
    // prime1 * LhsOr^2
    Value *LhsTot = Builder.CreateMul(LhsSquare, prime1);
    registerInteger(*LhsTot);

    // rhs
    // The same as lhs with prime2, any2 and y
    Value *RhsCast =
        Builder.CreateZExtOrTrunc(IntegerVect.at(Index2), IntermediaryType);
    registerInteger(*RhsCast);
    Value *RhsAnd = Builder.CreateAnd(RhsCast, OverflowMask);
    registerInteger(*RhsAnd);
    Value *RhsOr = Builder.CreateOr(RhsAnd, any2);
    registerInteger(*RhsOr);
    Value *RhsSquare = Builder.CreateMul(RhsOr, RhsOr);
    registerInteger(*RhsSquare);
    Value *RhsTot = Builder.CreateMul(RhsSquare, prime2);
    registerInteger(*RhsTot);

    // The final comparison always returning false
    Value *comp =
        Builder.CreateICmp(CmpInst::Predicate::ICMP_EQ, LhsTot, RhsTot);
    registerInteger(*comp);
    // Casting the boolean '0' back to the type of the replaced operand
    Value *castComp = Builder.CreateZExt(comp, ReplacedType);
    registerInteger(*castComp);

    return castComp;
  }

OK!
Almost there... we need to call our new function in the main loop and explicitly replace the operand:

.. code:: C++

  bool runOnBasicBlock(BasicBlock &BB) override {
    IntegerVect.clear();
    bool modified = false;

    for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),
                                       end = BB.end();
         I != end; ++I) {
      Instruction &Inst = *I;
        for (size_t i = 0; i < Inst.getNumOperands(); ++i) {
          if (Constant *C = isValidCandidateOperand(Inst.getOperand(i))) {
            if (Value *New_val = replaceZero(Inst, C)) {
              Inst.setOperand(i, New_val);
              modified = true;
            } else {
              // If sthg wrong happens during the replacement,
              // almost certainly because IntegerVect is empty
              errs() << "MyPass: could not rand pick a variable for replacement\n";
            }
          }
        }
      registerInteger(Inst);
    }
    return modified;
  }

and here is the code full code (with the tabulated prime numbers):

.. code:: C++

    namespace {
      using prime_type = uint32_t;

    static const prime_type Prime_array[] = {
         2 ,    3 ,    5 ,    7,     11,     13,     17,     19,     23,     29,
         31,    37,    41,    43,    47,     53,     59,     61,     67,     71,
         73,    79,    83,    89,    97,    101,    103,    107,    109,    113,
        127,   131,   137,   139,   149,    151,    157,    163,    167,    173,
        179,   181,   191,   193,   197,    199,    211,    223,    227,    229,
        233,   239,   241,   251,   257,    263,    269,    271,    277,    281,
        283,   293,   307,   311,   313,    317,    331,    337,    347,    349,
        353,   359,   367,   373,   379,    383,    389,    397,    401,    409,
        419,   421,   431,   433,   439,    443,    449,    457,    461,    463,
        467,   479,   487,   491,   499,    503,    509,    521,    523,    541,
        547,   557,   563,   569,   571,    577,    587,    593,    599,    601,
        607,   613,   617,   619,   631,    641,    643,    647,    653,    659,
        661,   673,   677,   683,   691,    701,    709,    719,    727,    733,
        739,   743,   751,   757,   761,    769,    773,    787,    797,    809,
        811,   821,   823,   827,   829,    839,    853,    857,    859,    863,
        877,   881,   883,   887,   907,    911,    919,    929,    937,    941,
        947,   953,   967,   971,   977,    983,    991,    997};

    class MyPass : public BasicBlockPass {
      std::vector<Value *> IntegerVect;
      std::default_random_engine Generator;

    public:

      static char ID;

      MyPass() : BasicBlockPass(ID) {}

      bool runOnBasicBlock(BasicBlock &BB) override {
        IntegerVect.clear();
        bool modified = false;

        // Not iterating from the beginning to avoid obfuscation of Phi instructions
        // parameters
        for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),
                                           end = BB.end();
             I != end; ++I) {
          Instruction &Inst = *I;
            for (size_t i = 0; i < Inst.getNumOperands(); ++i) {
              if (Constant *C = isValidCandidateOperand(Inst.getOperand(i))) {
                if (Value *New_val = replaceZero(Inst, C)) {
                  Inst.setOperand(i, New_val);
                  modified = true;
                } else {
                  errs() << "ObfuscateZero: could not rand pick a variable for replacement\n";
                }
              }
            }
          registerInteger(Inst);
        }

        return modified;
      }

    private:

      Constant *isValidCandidateOperand(Value *V) {
        Constant *C;
        if (!(C = dyn_cast<Constant>(V))) return nullptr;
        if (!C->isNullValue()) return nullptr;
        // We found a NULL constant, lets validate it
        if(!C->getType()->isIntegerTy()) {
          // dbgs() << "Ignoring non integer value\n";
          return nullptr;
        }
        return C;
      }

      void registerInteger(Value &V) {
        if (V.getType()->isIntegerTy())
          IntegerVect.push_back(&V);
      }

      // Return a random prime number not equal to DifferentFrom
      // If an error occurs returns 0
      prime_type getPrime(prime_type DifferentFrom = 0) {
          static std::uniform_int_distribution<prime_type> Rand(0, std::extend(decltype(Prime_array) - 1);
          size_t MaxLoop = 10;
          prime_type Prime;

          do {
                Prime = Prime_array[Rand(Generator)];
          } while(Prime == DifferentFrom && --MaxLoop);

          if(!MaxLoop) {
              return 0;
          }

          return Prime;
      }

      Value *replaceZero(Instruction &Inst, Value *VReplace) {
        // Replacing 0 by:
        // prime1 * ((x | any1)**2) != prime2 * ((y | any2)**2)
        // with prime1 != prime2 and any1 != 0 and any2 != 0
        prime_type p1 = getPrime(),
                   p2 = getPrime(p1);

        if(p2 == 0 || p1 == 0)
            return nullptr;

        Type *ReplacedType = VReplace->getType(),
             *IntermediaryType = IntegerType::get(Inst.getParent()->getContext(),
                                                  sizeof(prime_type) * 8);

        if (IntegerVect.empty()) {
          return nullptr;
        }

        std::uniform_int_distribution<size_t> Rand(0, IntegerVect.size() - 1);
        std::uniform_int_distribution<size_t> RandAny(1, 10);

        size_t Index1 = Rand(Generator), Index2 = Rand(Generator);

        // Masking Any1 and Any2 to avoid overflow in the obsfuscation
        Constant *any1 = ConstantInt::get(IntermediaryType, 1 + RandAny(Generator)),
                 *any2 = ConstantInt::get(IntermediaryType, 1 + RandAny(Generator)),
                 *prime1 = ConstantInt::get(IntermediaryType, p1),
                 *prime2 = ConstantInt::get(IntermediaryType, p2),
                 // Bitmask to prevent overflow
                 *OverflowMask = ConstantInt::get(IntermediaryType, 0x00000007);

        IRBuilder<> Builder(&Inst);

        // lhs
        // To avoid overflow
        Value *LhsCast =
            Builder.CreateZExtOrTrunc(IntegerVect.at(Index1), IntermediaryType);
        registerInteger(*LhsCast);
        Value *LhsAnd = Builder.CreateAnd(LhsCast, OverflowMask);
        registerInteger(*LhsAnd);
        Value *LhsOr = Builder.CreateOr(LhsAnd, any1);
        registerInteger(*LhsOr);
        Value *LhsSquare = Builder.CreateMul(LhsOr, LhsOr);
        registerInteger(*LhsSquare);
        Value *LhsTot = Builder.CreateMul(LhsSquare, prime1);
        registerInteger(*LhsTot);

        // rhs
        Value *RhsCast =
            Builder.CreateZExtOrTrunc(IntegerVect.at(Index2), IntermediaryType);
        registerInteger(*RhsCast);
        Value *RhsAnd = Builder.CreateAnd(RhsCast, OverflowMask);
        registerInteger(*RhsAnd);
        Value *RhsOr = Builder.CreateOr(RhsAnd, any2);
        registerInteger(*RhsOr);
        Value *RhsSquare = Builder.CreateMul(RhsOr, RhsOr);
        registerInteger(*RhsSquare);
        Value *RhsTot = Builder.CreateMul(RhsSquare, prime2);
        registerInteger(*RhsTot);

        // comp
        Value *comp =
            Builder.CreateICmp(CmpInst::Predicate::ICMP_EQ, LhsTot, RhsTot);
        registerInteger(*comp);
        Value *castComp = Builder.CreateZExt(comp, ReplacedType);
        registerInteger(*castComp);

        return castComp;
      }
    };
    }

DOOOOOOOOOOOOOOOOOOOOONE!

Let's try this awesome pass! If we use it on the last version of our test code we get:

.. code:: llvm

    ; Function Attrs: nounwind uwtable
    define i32 @main() #0 {
      %1 = alloca i32, align 4
      %a = alloca i32, align 4
      store i32 0, i32* %1
      store i32 2, i32* %a, align 4
      %2 = call i32 @puts(i8* getelementptr inbounds ([13 x i8]* @.str, i32 0, i32 0))
      %3 = load i32* %a, align 4
      %4 = mul nsw i32 %3, 3
      store i32 %4, i32* %a, align 4
      %5 = and i32 %3, 7
      %6 = or i32 %5, 2
      %7 = mul i32 %6, %6
      %8 = mul i32 %7, 719
      %9 = and i32 %2, 7
      %10 = or i32 %9, 8
      %11 = mul i32 %10, %10
      %12 = mul i32 %11, 397
      %13 = icmp eq i32 %8, %12
      %14 = zext i1 %13 to i32
      ret i32 %14
    }

Look at the assignments %5 to %14, looks familiar? We have successfully obfuscated the ``return 0`` instruction with the expression we gave at the beginning.

But there are a few important things left to read, so stay tunned!


You didn't think it would be that easy?
+++++++++++++++++++++++++++++++++++++++

The optimizer is your enemy
~~~~~~~~~~~~~~~~~~~~~~~~~~~

So far we have not tried to optimize our code.
But the compiler could optimize away some of your obfuscations and turn the code back to its original form.
Our obfuscation depends on some rather complex arithmetic properties so we are safe but you should keep in mind that the compiler might be working against you.

Even though our arithmetic is optimization-proof the rest of the code is not. The optimizer can still modify your code and delete all the candidate variables for x and y. If you want to see this effect, comment out the ``puts`` call in our test code and add the -O3 flag to your compilation command.

You should get this:

.. code:: llvm

    ; Function Attrs: nounwind readnone uwtable
    define i32 @main() #0 {
      ret i32 0
    }

In this case the compiler has optimized out ``a`` which was the only integer available for the obfuscation.
This explains why the obfuscation aborted.

Even if it is frustrating it is not a real problem, since the compiler won't delete all the potential integer in a real code.
However this is very annoying when writing tests.
The easiest work-around is to declare ``volatile`` the variables you don't want to be optimized out.

You might think that not using the optimizer is a good solution but:
    * If your obfuscation can't resist an optimizer, it won't resist reverse engineers.
    * Obfuscation often makes your program run slower, take more memory... So optimizing your obfuscated code might help mitigate these drawbacks.
    * Optimization can introduce some randomness in your obfuscations which would make your obfuscation patterns harder to recognize.


Final modification
~~~~~~~~~~~~~~~~~~

Now let's go back to our pass code for the last time.
So far we have supposed that we could replace *any* integer operand of *any* instruction.
Well, this is not actually true. Let's study the following code:

.. code:: C

    struct s {
        char a;
        int b;
    };

    int main() {
        struct s s1;
        int a = 3;

        s1.a = a;

        return 0;
    }

In LLVM bytecode access to structure members turns into the ``GetElementPointer`` instruction. It looks like this:

.. code:: llvm

    %4 = getelementptr inbounds %struct.s* %s1, i32 0, i32 0

As you can see there are two integer operands at the end. The first one is used when going through an array, so in our case it will always be 0.
The second one is the index of structure member we are accessing. If you access ``s.a`` it will be 0 and it will be 1 for ``s.b``.
For more info on ``getelementptr``, see http://llvm.org/docs/GetElementPtr.html.

The array index can be a literal, or a variable, this is why we can write ``array[i]``.
So our obfuscation can safely replace this operand by a variable if it was a literal 0.
**But** the tricky thing is that the second index *has* to be a literal, it can not be a variable.
But our obfuscation is going to replace this literal by a new variable if it is equal to zero.

I'm sure you want to know what happens when our pass breaks LLVM laws (clue: nothing to do with the FBI).
Well compile the above code with your pass and no optimizations and see for yourself.
Don't generate the LLVM bytecode, generate the binary (i.e remove the ``-S -emit-llvm`` options).
You should get a segfault... Not ideal, our pass makes compilation crash...

To solve this we just have to filter the type of instruction we are obfuscating.
We need to add a new function and add a new condition in our main loop:

.. code:: C++

  bool runOnBasicBlock(BasicBlock &BB) override {
    IntegerVect.clear();
    bool modified = false;

    // Not iterating from the beginning to avoid obfuscation of Phi instructions
    // parameters
    for (typename BasicBlock::iterator I = BB.getFirstInsertionPt(),
                                       end = BB.end();
         I != end; ++I) {
      Instruction &Inst = *I;
      if (isValidCandidateInstruction(Inst)) {
        for (size_t i = 0; i < Inst.getNumOperands(); ++i) {
          if (Constant *C = isValidCandidateOperand(Inst.getOperand(i))) {
            if (Value *New_val = replaceZero(Inst, C)) {
              Inst.setOperand(i, New_val);
              modified = true;
            } else {
              //dbgs() << "ObfuscateZero: could not rand pick a variable for replacement\n";
            }
          }
        }
      }
      registerInteger(Inst);
    }


  bool isValidCandidateInstruction(Instruction &Inst) {
    if (isa<GetElementPtrInst>(&Inst)) {
      // dbgs() << "Ignoring GEP\n";
      return false;
    } else if (isa<SwitchInst>(&Inst)) {
      // dbgs() << "Ignoring Switch\n";
      return false;
    } else if (isa<CallInst>(&Inst)) {
      // dbgs() << "Ignoring Calls\n";
      return false;
    } else {
      return true;
    }
  }

Pretty easy, no? Well the hard part is that this kind of problems is almost impossible to anticipate unless you know all the LLVM instructions.
The only solution to find this id to run your passes on big projects, see where it crashes and find out why.

Your code should now be pretty close to the ``ObfuscateZero`` pass.
And since I don't want to dump all the code on this page (again) from now on we are going to use the ObfuscateZero pass for our tests.

Tests, tests and more tests
~~~~~~~~~~~~~~~~~~~~~~~~~~~

I hope this last part made you understand that validation is critical before using your obfuscations in prod.
For ObfuscateZero we used the ``lit`` testing tool (LLVM Integrated Tester) we installed earlier.
This tool runs the tests you specify with a particular syntax. Take a look in the files in the ``$PASSDIR/tests/ObfuscateZero`` folder to learn how to use it.

For ObufuscateZero we have two types of tests:
    * Simple tests checking if the pass actually does what we want and doesn't crash in some tricky cases (GEP :p)
    * The validation scripts (*.sh files). Those files download the sources from openssl and zlib, compile them with our pass and run their validation suite. If the project compiles without error *and* passes its validation suite, we can suppose that our pass doesn't introduce bugs.

If you have installed ``lit`` then go to ``$PASSDIR/build`` and run:

.. code:: bash

    >$ make check

This will run the ``ObfuscateZero`` tests, which you can modify to test your pass. But it's going to take some time.
To validate ``ObfuscateZero`` we also compiled a C++ code since some constructs are not present when compiling from C.
However the test file has not been shipped in the git.

This is just the beginning
==========================

This tutorial was just an introduction to writing LLVM passes and using them for obfuscation.
There are many more funny things to do to make your code very annoying for reverse engineers.
I hope this will help you get started.
But remember, if you choose the quick and easy path as Vader did - you will become an agent of evil.


Thanks
======

- Kevin Szkudlapski, for the careful proof reading
- Mehdi Amini, for the extreme code review
- Jeanne Marcel, the ghostly presence
- and Serge Guelton, for the supreme coaching!
