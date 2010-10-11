
namespace klee {
namespace clvr {

//===----------------------------------------------------------------------===//
//
// SocketLog is an abstract interface that representes a sequence of buffers
// that were sent or received by an authoritative server. between a client and
// a server. There should be 1 per session. The SocketLog object reads from
// file and creates an ordered set of LogBuffer objects which represent the
// messages sent and received by the client. SocketLog also handles parsing and
// creating the associated LogBufferInfo object for each LogBuffer.
//
//===----------------------------------------------------------------------===//
	class SocketLog {

	}

//===----------------------------------------------------------------------===//
//
// SocketManager is an abstract interface that keeps track of a current
// execution state's position in the SocketLog. It uses whatever information is
// needed to determine if a write operation or a read operation is
// valid/satisfiable. Each ExecutionState has a SocketManager. 
//
//===----------------------------------------------------------------------===//
	class SocketManager {

	}

//===----------------------------------------------------------------------===//
//
// LogBuffer is an abstract class that holds a symbolic or concrete region of
// raw bytes and may also hold metadata in the form of a LogBufferInfo object.
// For example, in XPilot, the metadata would include the round number in which
// a message was sent.
//
//===----------------------------------------------------------------------===//
	class LogBuffer {}

	class LogBufferInfo {}

//===----------------------------------------------------------------------===//
//
// ExecutionStateInfo is an abstract interface that contains highlevel
// information, like the current round number. This information could be
// gathered in various ways; the client could be modified to call klee_*
// functions or the AddressSpace could be parsed to extract information. This
// metadata is used path searching, execution state merging and pruning and
// verfication against context information in a LogBufferInfo. This class
// should be extended and modified for each application that we want to verify.
// ExecutionStateInfo is used to sort the ExecutionStates to determine which
// should be executed next. ExecutionStates
//
//===----------------------------------------------------------------------===//
	class ExecutionStateInfo {

		virtual ExecutionStateInfo(ExecutionState& es);
		virtual void update();
	}

//===----------------------------------------------------------------------===//
//
// ExecutionStateScorer is used to sort the ExecutionStates to determine which
// should be executed next. This class could be extended and modified for each
// application that we want to verify. ExecutionStateScorer implementations may
// use various data to produce a score set, it might be paired with an app
// specific ExecutionStateInfo implemention to produce an application specific
// score or it may be generic and perform static analysis or even maintain
// somesort of branch predicition history and logic.
//
//===----------------------------------------------------------------------===//
	class ExecutionStateScorer {

		// compute() takes a set of ExecutionState objects and computes a score
		// value for each. The algorithm for computing the score will vary between
		// implementation. 
		virtual void compute(std::set<ExecutionState&> esv);

		// score() returns a value between 0 and 1 that is a measure of how likely
		// the ExecutionState associated with this object will be successful at the
		// next socket event or at the next branch, depending on the
		// implementation.
		virtual double score(ExecutionState &es);

		// weight() returns the a measure of the importance that should be
		// attributed to this object's score(). This may be a constant value set at
		// initialization or it may change over time as the object is given
		// feedback on it's success.
		virtual double weight();

	}

//===----------------------------------------------------------------------===//
//
// ExecutionStatePriorityQueue is an abstract interface for an object that
// maintains an ordering of ExecutionStates to determine which is to be
// executed next. An ExecutionStatePriorityQueue maintains one or more
// ExecutionStateScorer objects that it uses to determine the ordering.
//
//===----------------------------------------------------------------------===//
	class ExecutionStatePriorityQueue {
			virtual ExecutionState& pop();
			virtual void setValidity(ExecutionState& es, boolean validity);
	}

//===----------------------------------------------------------------------===//
//
// ExecutionStatePruner is an abstract interface for a class that prunes
// ExecutionStates, meaning it removes, simplifies or canonicalizes the
// components of an ExecutionState(AddressSpace, symbolic variables,
// constraints). The intention being to allow multiple ExecutionStates to
// become equivalent so that they can be merged. These classes may be chained
// (liked Solvers) to keep their implementations simple.
//
//===----------------------------------------------------------------------===//
	class ExecutionStatePruner {

	}

//===----------------------------------------------------------------------===//
//
// ExecutionStateMerger is a abstract interface for a class that takes two or
// more ExecutionStates and returns a merged version of the input if possible.
// The most basic merger simply checks for exact equivalence, a more
// sophisticated merger could combine constraints or memory contents into
// disjunctions for example. It also might be possible to design this object so
// that they may be chained 
//
//===----------------------------------------------------------------------===//
	class ExecutionStateMerger {

	}

//===----------------------------------------------------------------------===//
//
// ExecutionStateSerializer manages the serialization of ExecutionState objects
// so that they may be stored to disk. This might be done if system memory is
// low. (LOW PRIORITY)
//
//===----------------------------------------------------------------------===//
	class ExecutionStateSerializer {

	}

//===----------------------------------------------------------------------===//
//
// CliverSearcher implements the klee::Searcher interface, which is used to
// define different strategies for determining which ExecutionState is to be
// executed next. The goals of CliverSearcher are a modular design, utilizing
// one or more ExecutionStatePruner and ExecutionStateMerger classes.
//
//===----------------------------------------------------------------------===//
	class CliverSearcher {

	}

} // end clvr namespace 
} // end klee namespace 

namespace klee {

//===----------------------------------------------------------------------===//
//
// The MemoryManager is essentially a MemoryObject factory that generates new
// MemoryObjects on request. Every allocation is tracked so that a later time
// unreferenced allocations can be freed. Only one MemoryManager exists per
// context.
//
//===----------------------------------------------------------------------===//
	class MemoryManager {

	}

} // end klee namespace 
