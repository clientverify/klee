//===-- ConstraintPruner.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CONSTRAINT_PRUNER_H
#define CONSTRAINT_PRUNER_H


namespace cliver {

class AddressSpaceGraph;
class CVExecutionState;

class ConstraintPruner {
 public:
	ConstraintPruner();
	virtual void prune( CVExecutionState &state, AddressSpaceGraph &graph );

 private:

};

} // end namespace cliver
#endif // CONSTRAINT_PRUNER_H
