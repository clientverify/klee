
#ifndef TASE_UF_ARRAYS_H
#define TASE_UF_ARRAYS_H



//Implementation from wikipedia

#include "klee/Expr.h"
#include "klee/util/ExprHashMap.h"
#include <vector>

namespace klee {

  class UFElement {
  public:
    UFElement * parent;
    klee::Array * arr;
    int rank;
    int size;

    std::vector<ref<Expr>> constraints; //Only for representative of set

    UFElement(klee::Array * _a){
      arr = _a;
    }
    
  };

  UFElement * UF_MakeSet (UFElement * x) {
    if (x->arr == NULL) {
      printf("FATAL ERROR: arr not set in MakeSet operation \n");
      fflush(stdout);
    }
    x->parent = x;
    x->rank = 0;
    x->size = 1;

    return x;
  }

  //Path compression
  UFElement * UF_Find (UFElement *x) {
    if (x->parent != x)
      x->parent = UF_Find(x->parent);
    return x->parent;
  }

  UFElement * UF_Union (UFElement * x, UFElement * y) {
    UFElement * xRep = UF_Find(x);
    UFElement * yRep = UF_Find(y);
    if (xRep == yRep)
      return xRep; //Nothing to do; already in same set.

    //Save constraints for a merge later
    std::vector<ref<Expr>> * xConstraints = &(x->constraints);
    std::vector<ref<Expr>> * yConstraints = &(y->constraints);

    //Merge and update rank
    //Case 1: Merge y into x
    //Also breaks tie if ranks equal
    if (xRep->rank >= yRep->rank) {
      yRep->parent = xRep;
      if (xRep->rank == yRep->rank)
	xRep->rank++;
      xConstraints->insert(xConstraints->end(), yConstraints->begin(), yConstraints->end());
      yConstraints->clear();
      return xRep;
    } else {
      //Case 2: Merge x into y
      xRep->parent = yRep;
      if (yRep->rank == xRep->rank)
	yRep->rank++;
      yConstraints->insert(yConstraints->end(), xConstraints->begin(), xConstraints->end());
      xConstraints->clear();
      return yRep;
    }

  }
}


#endif
