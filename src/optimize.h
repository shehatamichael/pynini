// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Copyright 2017 and onwards Google, Inc.
//
// For general information on the Pynini grammar compilation library, see
// pynini.opengrm.org.

#ifndef PYNINI_OPTIMIZE_H_
#define PYNINI_OPTIMIZE_H_

// Generic optimization methods for FSTs, inspired by those originally included
// in Thrax.
//
// For more information on the optimization procedure, see:
//
// Allauzen, C., Mohri, M., Riley, M., and Roark, B. 2004. A generalized
// construction of integrated speech recognition transducers. In Proc. ICASSP,
// pages 761-764.

#include <type_traits>

#include <fst/types.h>
#include <fst/arcsort.h>
#include <fst/determinize.h>
#include <fst/encode.h>
#include <fst/minimize.h>
#include <fst/mutable-fst.h>
#include <fst/rmepsilon.h>
#include <fst/state-map.h>

// These functions are generic optimization methods for mutable FSTs, inspired
// by those originally included in Thrax.

namespace fst {
namespace internal {

constexpr uint64 kDoNotEncodeWeights = (kAcyclic | kUnweighted |
                                        kUnweightedCycles);

constexpr uint64 kDifferenceRhsProperties = kUnweighted | kAcceptor;

// Helpers.

// Calls RmEpsilon if the FST is not (known to be) epsilon-free.
template <class Arc>
void MaybeRmEpsilon(MutableFst<Arc> *fst, bool compute_props = false) {
  if (fst->Properties(kNoEpsilons, compute_props) != kNoEpsilons) {
    RmEpsilon(fst);
  }
}

template <class Arc>
void DeterminizeAndMinimize(MutableFst<Arc> *fst) {
  Determinize(*fst, fst);
  Minimize(fst);
}

// Optimizes the FST according to the encoder flags:
//
//   kEncodeLabels: optimize as a weighted acceptor
//   kEncodeWeights: optimize as an unweighted transducer
//   kEncodeLabels | kEncodeWeights: optimize as an unweighted acceptor
template <class Arc>
void OptimizeAs(MutableFst<Arc> *fst, uint8 flags) {
  EncodeMapper<Arc> encoder(flags);
  Encode(fst, &encoder);
  DeterminizeAndMinimize(fst);
  Decode(fst, encoder);
}

// Generic FST optimization function to be used when the FST is known to be an
// acceptor. Version for FSTs with non-idempotent weights, limiting
// optimization possibilities.
template <class Arc,
          typename std::enable_if<(Arc::Weight::Properties() & kIdempotent) !=
                                  kIdempotent>::type * = nullptr>
void OptimizeAcceptor(MutableFst<Arc> *fst, bool compute_props = false) {
  // If the FST is not (known to be) epsilon-free, perform epsilon-removal.
  MaybeRmEpsilon(fst, compute_props);
  // The FST has non-idempotent weights; limiting optimization possibilities.
  if (fst->Properties(kIDeterministic, compute_props) != kIDeterministic) {
    // But "any acyclic weighted automaton over a zero-sum-free semiring has
    // the twins property and is determinizable" (Mohri 2006).
    if (fst->Properties(kAcyclic, compute_props) == kAcyclic) {
      DeterminizeAndMinimize(fst);
    }
  } else {
    Minimize(fst);
  }
}

template <class Arc,
          typename std::enable_if<(Arc::Weight::Properties() & kIdempotent) ==
                                  kIdempotent>::type * = nullptr>
void OptimizeAcceptor(MutableFst<Arc> *fst, bool compute_props = false) {
  // If the FST is not (known to be) epsilon-free, perform epsilon-removal.
  MaybeRmEpsilon(fst, compute_props);
  // If the FST is not (known to be) deterministic, determinize it.
  if (fst->Properties(kIDeterministic, compute_props) != kIDeterministic) {
    // If the FST is not known to have no weighted cycles, it is encoded
    // before determinization and minimization.
    if (!fst->Properties(kDoNotEncodeWeights, compute_props)) {
      OptimizeAs(fst, kEncodeWeights);
      // Combines any remaining muti-arcs.
      StateMap(fst, ArcSumMapper<Arc>(*fst));
    } else {
      DeterminizeAndMinimize(fst);
    }
  } else {
    Minimize(fst);
  }
}

// Generic FST optimization function to be used when the FST is (may be) a
// transducer. Version for FSTs with non-idempotent weights, limiting
// optimization possibilities.
template <class Arc,
          typename std::enable_if<(Arc::Weight::Properties() & kIdempotent) !=
                                  kIdempotent>::type * = nullptr>
void OptimizeTransducer(MutableFst<Arc> *fst, bool compute_props = false) {
  // If the FST is not (known to be) epsilon-free, perform epsilon-removal.
  MaybeRmEpsilon(fst, compute_props);
  // The FST has non-idempotent weights; limiting optimization possibilities.
  if (fst->Properties(kIDeterministic, compute_props) != kIDeterministic) {
    // But "any acyclic weighted automaton over a zero-sum-free semiring has
    // the twins property and is determinizable" (Mohri 2006). We just have to
    // encode labels.
    if (fst->Properties(kAcyclic, compute_props)) {
      OptimizeAs(fst, kEncodeLabels);
    }
  } else {
    Minimize(fst);
  }
}

template <class Arc,
          typename std::enable_if<(Arc::Weight::Properties() & kIdempotent) ==
                                  kIdempotent>::type * = nullptr>
void OptimizeTransducer(MutableFst<Arc> *fst, bool compute_props = false) {
  // If the FST is not (known to be) epsilon-free, perform epsilon-removal.
  MaybeRmEpsilon(fst, compute_props);
  // If the FST is not (known to be) deterministic, determinize it.
  if (fst->Properties(kIDeterministic, compute_props) != kIDeterministic) {
    // FST labels are always encoded before determinization and minimization.
    // If the FST is not known to have no weighted cycles, its weights are
    // also encoded before determinization and minimization.
    if (!fst->Properties(kDoNotEncodeWeights, compute_props)) {
      OptimizeAs(fst, kEncodeLabels | kEncodeWeights);
      // Combines any remaining muti-arcs.
      StateMap(fst, ArcSumMapper<Arc>(*fst));
    } else {
      OptimizeAs(fst, kEncodeLabels);
    }
  } else {
    Minimize(fst);
  }
}

}  // namespace internal

// Generic FST optimization function; use the more-specialized forms below if
// the FST is known to be an acceptor or a transducer.

// Destructive signature.
template <class Arc>
void Optimize(MutableFst<Arc> *fst, bool compute_props = false) {
  if (fst->Properties(kAcceptor, compute_props) != kAcceptor) {
    // The FST is (may be) a transducer.
    internal::OptimizeTransducer(fst, compute_props);
  } else {
    // The FST is (known to be) an acceptor.
    internal::OptimizeAcceptor(fst, compute_props);
  }
}

// This function optimizes the right-hand side of an FST difference in an
// attempt to satisfy the constraint that it must be epsilon-free and
// deterministic. The input is assumed to be an unweighted acceptor.
template <class Arc>
void OptimizeDifferenceRhs(MutableFst<Arc> *fst, bool compute_props = false) {
  // If the FST is not (known to be) epsilon-free, performs epsilon-removal.
  internal::MaybeRmEpsilon(fst, compute_props);
  // If the FST is not (known to be) deterministic, determinizes it; note that
  // this operation will not introduce epsilons as the input is an acceptor.
  if (fst->Properties(kIDeterministic, compute_props) != kIDeterministic) {
    Determinize(*fst, fst);
  }
  // Minimally, RHS must be input label-sorted; the LHS does not need
  // arc-sorting when the RHS is deterministic (as it now should be).
  static const ILabelCompare<Arc> comp;
  ArcSort(fst, comp);
}

}  // namespace fst

#endif  // PYNINI_OPTIMIZE_H_

