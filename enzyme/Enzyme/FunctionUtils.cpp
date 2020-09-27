//===- FunctionUtils.cpp - Implementation of function utilities -----------===//
//
//                             Enzyme Project
//
// Part of the Enzyme Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// If using this code in an academic setting, please cite the following:
// @incollection{enzymeNeurips,
// title = {Instead of Rewriting Foreign Code for Machine Learning, Automatically Synthesize Fast Gradients},
// author = {Moses, William S. and Churavy, Valentin},
// booktitle = {Advances in Neural Information Processing Systems 33},
// year = {2020},
// note = {To appear in},
// }
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities on LLVM Functions that are used as part of the AD
// process.
//
//===----------------------------------------------------------------------===//
#include "FunctionUtils.h"

#include "EnzymeLogic.h"
#include "GradientUtils.h"
#include "LibraryFuncs.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"

#include "llvm/Analysis/TypeBasedAliasAnalysis.h"

#if LLVM_VERSION_MAJOR > 6
#include "llvm/Analysis/PhiValues.h"
#endif
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetTransformInfo.h"

#include "llvm/Transforms/IPO/FunctionAttrs.h"

#include "llvm/Transforms/Utils.h"

#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#if LLVM_VERSION_MAJOR > 6
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#endif

#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"

#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/LCSSA.h"

#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"

using namespace llvm;

static cl::opt<bool>
    enzyme_preopt("enzyme_preopt", cl::init(true), cl::Hidden,
//    enzyme_preopt("enzyme_preopt", cl::init( (LLVM_VERSION_MAJOR <= 7) ? true : false), cl::Hidden,
                  cl::desc("Run enzyme preprocessing optimizations"));

static cl::opt<bool> autodiff_inline("enzyme_inline", cl::init(false),
                                     cl::Hidden,
                                     cl::desc("Force inlining of autodiff"));

static cl::opt<int>
    autodiff_inline_count("enzyme_inline_count", cl::init(10000), cl::Hidden,
                          cl::desc("Limit of number of functions to inline"));

static bool promoteMemoryToRegister(Function &F, DominatorTree &DT,
                                    AssumptionCache &AC) {
  std::vector<AllocaInst *> Allocas;
  BasicBlock &BB = F.getEntryBlock(); // Get the entry node for the function
  bool Changed = false;

  while (true) {
    Allocas.clear();

    // Find allocas that are safe to promote, by looking at all instructions in
    // the entry node
    for (BasicBlock::iterator I = BB.begin(), E = --BB.end(); I != E; ++I)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) // Is it an alloca?
        if (isAllocaPromotable(AI))
          Allocas.push_back(AI);

    if (Allocas.empty())
      break;

    PromoteMemToReg(Allocas, DT, &AC);
    Changed = true;
  }
  return Changed;
}

void identifyRecursiveFunctions(Function *F,
                                std::map<const Function *, int> &seen) {
  // haven't seen this function before
  if (seen[F] == 0) {
    seen[F] = 1; // staging
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if (auto call = dyn_cast<CallInst>(&*I)) {
        if (call->getCalledFunction() == nullptr)
          continue;
        if (call->getCalledFunction()->empty())
          continue;
        identifyRecursiveFunctions(call->getCalledFunction(), seen);
      }
    }
    if (seen[F] == 1) {
      seen[F] = 2; // not recursive
    }
  } else if (seen[F] == 1) {
    seen[F] = 3; // definitely recursive
  }
}

void forceRecursiveInlining(Function *NewF, const Function *F) {
  std::map<const Function *, int> seen;
  identifyRecursiveFunctions(NewF, seen);

  int count = 0;
remover:
  SmallPtrSet<Instruction *, 10> originalInstructions;
  for (inst_iterator I = inst_begin(NewF), E = inst_end(NewF); I != E; ++I) {
    originalInstructions.insert(&*I);
  }
  if (count >= autodiff_inline_count)
    return;
  for (inst_iterator I = inst_begin(NewF), E = inst_end(NewF); I != E; ++I)
    if (auto call = dyn_cast<CallInst>(&*I)) {
      // if (isconstantM(call, constants, nonconstant, returnvals,
      // originalInstructions)) continue;
      if (call->getCalledFunction() == nullptr)
        continue;
      if (call->getCalledFunction()->empty())
        continue;
      /*
      if (call->getCalledFunction()->hasFnAttribute(Attribute::NoInline)) {
          llvm::errs() << "can't inline noinline " <<
      call->getCalledFunction()->getName() << "\n"; continue;
      }
      */
      if (call->getCalledFunction()->hasFnAttribute(Attribute::ReturnsTwice))
        continue;
      if (seen[call->getCalledFunction()] == 3) {
        llvm::errs() << "can't inline recursive "
                     << call->getCalledFunction()->getName() << "\n";
        continue;
      }
      InlineFunctionInfo IFI;
      #if LLVM_VERSION_MAJOR >= 11
      InlineFunction(*call, IFI);
      #else
      InlineFunction(call, IFI);
      #endif
      ++count;
      if (count >= autodiff_inline_count)
        break;
      else
        goto remover;
    }
}

PHINode *canonicalizeIVs(fake::SCEVExpander &e, Type *Ty, Loop *L,
                         DominatorTree &DT, GradientUtils *gutils) {
  PHINode *CanonicalIV = e.getOrInsertCanonicalInductionVariable(L, Ty);
  assert(CanonicalIV && "canonicalizing IV");

  // This ensures that SE knows that the canonical IV doesn't wrap around
  // This is permissible as Enzyme may assume that your program doesn't have an
  // infinite loop (and thus will never end)
  for (auto &a : CanonicalIV->incoming_values()) {
    if (auto add = dyn_cast<BinaryOperator>(a.getUser())) {
      add->setHasNoUnsignedWrap(true);
      add->setHasNoSignedWrap(true);
    }
  }

  SmallVector<WeakTrackingVH, 16> DeadInst0;
  e.replaceCongruentIVs(L, &DT, DeadInst0);

  for (WeakTrackingVH V : DeadInst0) {
    gutils->erase(cast<Instruction>(V)); //->eraseFromParent();
  }

  return CanonicalIV;
}

Function *preprocessForClone(Function *F, AAResults &AA, TargetLibraryInfo &TLI,
                             bool topLevel) {
  static std::map<Function *, Function *> cache;
  // static std::map<Function*, BasicAAResult*> cache_AA;
  if (cache.find(F) != cache.end()) {

    Function *NewF = cache[F];
    AssumptionCache *AC = new AssumptionCache(*NewF);
    DominatorTree *DTL = new DominatorTree(*NewF);
    LoopInfo *LI = new LoopInfo(*DTL);
#if LLVM_VERSION_MAJOR > 6
    PhiValues *PV = new PhiValues(*NewF);
#endif
    auto baa = new BasicAAResult(
        NewF->getParent()->getDataLayout(),
#if LLVM_VERSION_MAJOR > 6
        *NewF,
#endif
        TLI, *AC, DTL /*&AM.getResult<DominatorTreeAnalysis>(*NewF)*/, LI
#if LLVM_VERSION_MAJOR > 6
        ,
        PV
#endif
    );
    AA.addAAResult(*baa); //(cache_AA[F]));
    AA.addAAResult(*(new TypeBasedAAResult()));
    return cache[F];
  }
  Function *NewF =
      Function::Create(F->getFunctionType(), F->getLinkage(),
                       "preprocess_" + F->getName(), F->getParent());

  ValueToValueMapTy VMap;
  for (auto i = F->arg_begin(), j = NewF->arg_begin(); i != F->arg_end();) {
    VMap[i] = j;
    j->setName(i->getName());
    ++i;
    ++j;
  }

  SmallVector<ReturnInst *, 4> Returns;
  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns, "",
                    nullptr);
  NewF->setAttributes(F->getAttributes());

  if (enzyme_preopt) {

    if (autodiff_inline) {
      forceRecursiveInlining(NewF, F);
    }
  }

  {
    std::vector<Instruction *> freesToErase;
    for (auto &BB : *NewF) {
      for (auto &I : BB) {

        if (auto CI = dyn_cast<CallInst>(&I)) {

          Function *called = CI->getCalledFunction();
          #if LLVM_VERSION_MAJOR >= 11
          if (auto castinst = dyn_cast<ConstantExpr>(CI->getCalledOperand())) {
          #else
          if (auto castinst = dyn_cast<ConstantExpr>(CI->getCalledValue())) {
          #endif
            if (castinst->isCast()) {
              if (auto fn = dyn_cast<Function>(castinst->getOperand(0))) {
                if (isDeallocationFunction(*fn, TLI)) {
                  called = fn;
                }
              }
            }
          }

          if (called && isDeallocationFunction(*called, TLI)) {
            freesToErase.push_back(CI);
          }
        }
      }
    }
    for (auto f : freesToErase) {
      f->eraseFromParent();
    }
  }

  if (enzyme_preopt) {

    if (autodiff_inline) {
      {
        DominatorTree DT(*NewF);
        AssumptionCache AC(*NewF);
        promoteMemoryToRegister(*NewF, DT, AC);
      }

      {
        FunctionAnalysisManager AM;
        AM.registerPass([] { return AAManager(); });
        AM.registerPass([] { return ScalarEvolutionAnalysis(); });
        AM.registerPass([] { return AssumptionAnalysis(); });
        AM.registerPass([] { return TargetLibraryAnalysis(); });
        AM.registerPass([] { return TargetIRAnalysis(); });
        AM.registerPass([] { return MemorySSAAnalysis(); });
        AM.registerPass([] { return DominatorTreeAnalysis(); });
        AM.registerPass([] { return MemoryDependenceAnalysis(); });
        AM.registerPass([] { return LoopAnalysis(); });
        AM.registerPass([] { return OptimizationRemarkEmitterAnalysis(); });
#if LLVM_VERSION_MAJOR > 6
        AM.registerPass([] { return PhiValuesAnalysis(); });
#endif
        AM.registerPass([] { return LazyValueAnalysis(); });
#if LLVM_VERSION_MAJOR > 10
        AM.registerPass([] { return PassInstrumentationAnalysis(); });
#endif

        GVN().run(*NewF, AM);
        SROA().run(*NewF, AM);
      }
    }

    {
      DominatorTree DT(*NewF);
      AssumptionCache AC(*NewF);
      promoteMemoryToRegister(*NewF, DT, AC);
    }

    {
      FunctionAnalysisManager AM;
      AM.registerPass([] { return AAManager(); });
      AM.registerPass([] { return ScalarEvolutionAnalysis(); });
      AM.registerPass([] { return AssumptionAnalysis(); });
      AM.registerPass([] { return TargetLibraryAnalysis(); });
      AM.registerPass([] { return TargetIRAnalysis(); });
      AM.registerPass([] { return MemorySSAAnalysis(); });
      AM.registerPass([] { return DominatorTreeAnalysis(); });
      AM.registerPass([] { return MemoryDependenceAnalysis(); });
      AM.registerPass([] { return LoopAnalysis(); });
      AM.registerPass([] { return OptimizationRemarkEmitterAnalysis(); });
#if LLVM_VERSION_MAJOR > 6
      AM.registerPass([] { return PhiValuesAnalysis(); });
#endif
#if LLVM_VERSION_MAJOR >= 8
      AM.registerPass([] { return PassInstrumentationAnalysis(); });
#endif
      AM.registerPass([] { return LazyValueAnalysis(); });
#if LLVM_VERSION_MAJOR > 6
      // InstSimplifyPass().run(*NewF, AM);
#endif
      // InstCombinePass().run(*NewF, AM);

      // EarlyCSEPass(/*memoryssa*/true).run(*NewF, AM);

      // GVN().run(*NewF, AM);
      SROA().run(*NewF, AM);

      // CorrelatedValuePropagationPass().run(*NewF, AM);

      // DCEPass().run(*NewF, AM);
      // DSEPass().run(*NewF, AM);
      // MemCpyOptPass().run(*NewF, AM);
      #if LLVM_VERSION_MAJOR >= 12
      SimplifyCFGOptions scfgo;
      #else
      SimplifyCFGOptions scfgo(
          /*unsigned BonusThreshold=*/1, /*bool ForwardSwitchCond=*/false,
          /*bool SwitchToLookup=*/false, /*bool CanonicalLoops=*/true,
          /*bool SinkCommon=*/true, /*AssumptionCache *AssumpCache=*/nullptr);
      #endif
      SimplifyCFGPass(scfgo).run(*NewF, AM);
    }
  }

  // Run LoopSimplifyPass to ensure preheaders exist on all loops

  {
    FunctionAnalysisManager AM;
    AM.registerPass([] { return LoopAnalysis(); });
    AM.registerPass([] { return DominatorTreeAnalysis(); });
    AM.registerPass([] { return ScalarEvolutionAnalysis(); });
    AM.registerPass([] { return AssumptionAnalysis(); });
#if LLVM_VERSION_MAJOR >= 8
    AM.registerPass([] { return PassInstrumentationAnalysis(); });
#endif
#if LLVM_VERSION_MAJOR >= 11
    AM.registerPass([] { return MemorySSAAnalysis(); });
#endif
    LoopSimplifyPass().run(*NewF, AM);
  }

  {
    // Alias analysis is necessary to ensure can query whether we can move a
    // forward pass function BasicAA ba; auto baa = new
    // BasicAAResult(ba.run(*NewF, AM));
    AssumptionCache *AC = new AssumptionCache(*NewF);
    DominatorTree *DTL = new DominatorTree(*NewF);
    LoopInfo *LI = new LoopInfo(*DTL);
#if LLVM_VERSION_MAJOR > 6
    PhiValues *PV = new PhiValues(*NewF);
#endif
    auto baa = new BasicAAResult(
        NewF->getParent()->getDataLayout(),
#if LLVM_VERSION_MAJOR > 6
        *NewF,
#endif
        TLI, *AC, DTL /*&AM.getResult<DominatorTreeAnalysis>(*NewF)*/, LI
#if LLVM_VERSION_MAJOR > 6
        ,
        PV
#endif
    );
    AA.addAAResult(*baa);
    AA.addAAResult(*(new TypeBasedAAResult()));
    // ScopedNoAliasAA sa;
    // auto saa = new ScopedNoAliasAAResult(sa.run(*NewF, AM));
    // AA.addAAResult(*saa);
  }

  std::vector<AllocaInst *> toconvert;

  for (inst_iterator I = inst_begin(*NewF), E = inst_end(*NewF); I != E; ++I) {
    if (auto ai = dyn_cast<AllocaInst>(&*I)) {
      std::vector<std::pair<Value *, User *>> todo;
      for (auto a : ai->users())
        todo.push_back(std::make_pair((Value *)ai, a));
      std::set<User *> seen;
      bool usableEverywhere = ai->getParent() == &NewF->getEntryBlock();
      // TODO use is_value_needed_in_reverse
      if (!usableEverywhere || !topLevel) {
        toconvert.push_back(ai);
      }
    }
  }

  for (auto ai : toconvert) {
    std::string nam = ai->getName().str();
    ai->setName("");

    Instruction *insertBefore = ai;
    while (isa<AllocaInst>(insertBefore->getNextNode())) {
      insertBefore = insertBefore->getNextNode();
      assert(insertBefore);
    }

    auto i64 = Type::getInt64Ty(NewF->getContext());
    auto rep = CallInst::CreateMalloc(
        insertBefore, i64, ai->getAllocatedType(),
        ConstantInt::get(
            i64, NewF->getParent()->getDataLayout().getTypeAllocSizeInBits(
                     ai->getAllocatedType()) /
                     8),
        IRBuilder<>(insertBefore).CreateZExtOrTrunc(ai->getArraySize(), i64),
        nullptr, nam);
    assert(rep->getType() == ai->getType());
    ai->replaceAllUsesWith(rep);
    ai->eraseFromParent();
  }

  if (enzyme_print)
    llvm::errs() << "after simplification :\n" << *NewF << "\n";

  if (llvm::verifyFunction(*NewF, &llvm::errs())) {
    llvm::errs() << *NewF << "\n";
    report_fatal_error("function failed verification (1)");
  }
  cache[F] = NewF;
  return NewF;
}

Function *CloneFunctionWithReturns(
    bool topLevel, Function *&F, AAResults &AA, TargetLibraryInfo &TLI,
    ValueToValueMapTy &ptrInputs, const std::vector<DIFFE_TYPE> &constant_args,
    SmallPtrSetImpl<Value *> &constants, SmallPtrSetImpl<Value *> &nonconstant,
    SmallPtrSetImpl<Value *> &returnvals, ReturnType returnValue, Twine name,
    ValueToValueMapTy *VMapO, bool diffeReturnArg, llvm::Type *additionalArg) {
  assert(!F->empty());
  F = preprocessForClone(F, AA, TLI, topLevel);
  std::vector<Type *> RetTypes;
  if (returnValue == ReturnType::ArgsWithReturn ||
      returnValue == ReturnType::ArgsWithTwoReturns)
    RetTypes.push_back(F->getReturnType());
  if (returnValue == ReturnType::ArgsWithTwoReturns)
    RetTypes.push_back(F->getReturnType());
  std::vector<Type *> ArgTypes;

  ValueToValueMapTy VMap;

  // The user might be deleting arguments to the function by specifying them in
  // the VMap.  If so, we need to not add the arguments to the arg ty vector
  //
  unsigned argno = 0;
  for (const Argument &I : F->args()) {
    ArgTypes.push_back(I.getType());
    if (constant_args[argno] == DIFFE_TYPE::DUP_ARG ||
        constant_args[argno] == DIFFE_TYPE::DUP_NONEED) {
      ArgTypes.push_back(I.getType());
    } else if (constant_args[argno] == DIFFE_TYPE::OUT_DIFF) {
      RetTypes.push_back(I.getType());
    }
    ++argno;
  }

  for (BasicBlock &BB : *F) {
    for (Instruction &I : BB) {
      if (auto ri = dyn_cast<ReturnInst>(&I)) {
        if (auto rv = ri->getReturnValue()) {
          returnvals.insert(rv);
        }
      }
    }
  }

  if (diffeReturnArg) {
    assert(!F->getReturnType()->isVoidTy());
    ArgTypes.push_back(F->getReturnType());
  }
  if (additionalArg) {
    ArgTypes.push_back(additionalArg);
  }
  Type *RetType = StructType::get(F->getContext(), RetTypes);
  if (returnValue == ReturnType::TapeAndTwoReturns ||
      returnValue == ReturnType::TapeAndReturn ||
      returnValue == ReturnType::Tape) {
    RetTypes.clear();
    RetTypes.push_back(Type::getInt8PtrTy(F->getContext()));
    if (returnValue == ReturnType::TapeAndTwoReturns) {
      RetTypes.push_back(F->getReturnType());
      RetTypes.push_back(F->getReturnType());
    } else if (returnValue == ReturnType::TapeAndReturn) {
      RetTypes.push_back(F->getReturnType());
    }
    RetType = StructType::get(F->getContext(), RetTypes);
  }

  bool noReturn = RetTypes.size() == 0;
  if (noReturn)
    RetType = Type::getVoidTy(RetType->getContext());

  // Create a new function type...
  FunctionType *FTy =
      FunctionType::get(RetType, ArgTypes, F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF = Function::Create(FTy, F->getLinkage(), name, F->getParent());
  if (diffeReturnArg) {
    auto I = NewF->arg_end();
    I--;
    if (additionalArg)
      I--;
    I->setName("differeturn");
  }
  if (additionalArg) {
    auto I = NewF->arg_end();
    I--;
    I->setName("tapeArg");
  }

  {
    unsigned ii = 0;
    for (auto i = F->arg_begin(), j = NewF->arg_begin(); i != F->arg_end();) {
      VMap[i] = j;
      ++j;
      ++i;
      if (constant_args[ii] == DIFFE_TYPE::DUP_ARG ||
          constant_args[ii] == DIFFE_TYPE::DUP_NONEED) {
        ++j;
      }
      ++ii;
    }
  }

  // Loop over the arguments, copying the names of the mapped arguments over...
  Function::arg_iterator DestI = NewF->arg_begin();

  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) {     // Is this argument preserved?
      DestI->setName(I.getName()); // Copy the name over...
      VMap[&I] = &*DestI++;        // Add mapping to VMap
    }
  SmallVector<ReturnInst *, 4> Returns;
  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns, "",
                    nullptr);
  if (VMapO)
    VMapO->insert(VMap.begin(), VMap.end());

  bool hasPtrInput = false;
  unsigned ii = 0, jj = 0;
  for (auto i = F->arg_begin(), j = NewF->arg_begin(); i != F->arg_end();) {
    if (constant_args[ii] == DIFFE_TYPE::CONSTANT) {
      constants.insert(i);
      if (printconst)
        llvm::errs() << "in new function " << NewF->getName()
                     << " constant arg " << *j << "\n";
    } else {
      nonconstant.insert(i);
      if (printconst)
        llvm::errs() << "in new function " << NewF->getName()
                     << " nonconstant arg " << *j << "\n";
    }

    if (constant_args[ii] == DIFFE_TYPE::DUP_ARG ||
        constant_args[ii] == DIFFE_TYPE::DUP_NONEED) {
      hasPtrInput = true;
      ptrInputs[i] = (j + 1);
      if (F->hasParamAttribute(ii, Attribute::NoCapture)) {
        NewF->addParamAttr(jj + 1, Attribute::NoCapture);
      }

      j->setName(i->getName());
      ++j;
      j->setName(i->getName() + "'");
      nonconstant.insert(j);
      ++j;
      jj += 2;

      ++i;

    } else {
      j->setName(i->getName());
      ++j;
      ++jj;
      ++i;
    }
    ++ii;
  }

  if (hasPtrInput) {
    if (NewF->hasFnAttribute(Attribute::ReadNone)) {
      NewF->removeFnAttr(Attribute::ReadNone);
    }
    if (NewF->hasFnAttribute(Attribute::ReadOnly)) {
      NewF->removeFnAttr(Attribute::ReadOnly);
    }
  }
  NewF->setLinkage(Function::LinkageTypes::InternalLinkage);
  assert(NewF->hasLocalLinkage());

  return NewF;
}

static cl::opt<bool> autodiff_optimize("enzyme_optimize", cl::init(false),
                                       cl::Hidden,
                                       cl::desc("Force inlining of autodiff"));

void optimizeIntermediate(GradientUtils *gutils, bool topLevel, Function *F) {
  if (!autodiff_optimize)
    return;

  {
    DominatorTree DT(*F);
    AssumptionCache AC(*F);
    promoteMemoryToRegister(*F, DT, AC);
  }

  FunctionAnalysisManager AM;
  AM.registerPass([] { return AAManager(); });
  AM.registerPass([] { return ScalarEvolutionAnalysis(); });
  AM.registerPass([] { return AssumptionAnalysis(); });
  AM.registerPass([] { return TargetLibraryAnalysis(); });
  AM.registerPass([] { return TargetIRAnalysis(); });
  AM.registerPass([] { return MemorySSAAnalysis(); });
  AM.registerPass([] { return DominatorTreeAnalysis(); });
  AM.registerPass([] { return MemoryDependenceAnalysis(); });
  AM.registerPass([] { return LoopAnalysis(); });
  AM.registerPass([] { return OptimizationRemarkEmitterAnalysis(); });
#if LLVM_VERSION_MAJOR > 6
  AM.registerPass([] { return PhiValuesAnalysis(); });
#endif
  AM.registerPass([] { return LazyValueAnalysis(); });
  LoopAnalysisManager LAM;
  AM.registerPass([&] { return LoopAnalysisManagerFunctionProxy(LAM); });
  LAM.registerPass([&] { return FunctionAnalysisManagerLoopProxy(AM); });
  // LoopSimplifyPass().run(*F, AM);

  // TODO function attributes
  // PostOrderFunctionAttrsPass().run(*F, AM);
  GVN().run(*F, AM);
  SROA().run(*F, AM);
  EarlyCSEPass(/*memoryssa*/ true).run(*F, AM);
#if LLVM_VERSION_MAJOR > 6
  InstSimplifyPass().run(*F, AM);
#endif
  CorrelatedValuePropagationPass().run(*F, AM);

  DCEPass().run(*F, AM);
  DSEPass().run(*F, AM);

  createFunctionToLoopPassAdaptor(LoopDeletionPass()).run(*F, AM);

  #if LLVM_VERSION_MAJOR >= 12
  SimplifyCFGOptions scfgo = SimplifyCFGOptions();
  #else
  SimplifyCFGOptions scfgo(
      /*unsigned BonusThreshold=*/1, /*bool ForwardSwitchCond=*/false,
      /*bool SwitchToLookup=*/false, /*bool CanonicalLoops=*/true,
      /*bool SinkCommon=*/true, /*AssumptionCache *AssumpCache=*/nullptr);

  #endif
  SimplifyCFGPass(scfgo).run(*F, AM);
  // LCSSAPass().run(*NewF, AM);
}
