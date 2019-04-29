#include "klee/ExecutionState.h"
#include "klee/CVAssignment.h"
#include "klee/util/ExprUtil.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/Constraints.h"
#include "klee/util/ArrayCache.h"
#include "../Core/ImpliedValue.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "llvm/Support/CommandLine.h"
#include "../Core/Executor.h"
#include <iostream>



CVAssignment::CVAssignment(std::vector<const klee::Array*> &objects,
			   std::vector< std::vector<unsigned char> > &values) {
  addBindings(objects, values);
}

void CVAssignment::addBindings(std::vector<const klee::Array*> &objects,
			       std::vector< std::vector<unsigned char> > &values) {

  std::vector< std::vector<unsigned char> >::iterator valIt =
    values.begin();
  for (std::vector<const klee::Array*>::iterator it = objects.begin(),
	 ie = objects.end(); it != ie; ++it) {
    const klee::Array *os = *it;
    std::vector<unsigned char> &arr = *valIt;
    bindings.insert(std::make_pair(os, arr));
    name_bindings.insert(std::make_pair(os->name, os));
    ++valIt;
  }
}

//Extended for TASE to include ExecStatePtr
void CVAssignment::solveForBindings(klee::Solver* solver,
				    klee::ref<klee::Expr> &expr,
				    klee::ExecutionState * ExecStatePtr) {
  std::vector<const klee::Array*> arrays;
  std::vector< std::vector<unsigned char> > initial_values;

  klee::findSymbolicObjects(expr, arrays);
  //ABH: It needs to be the case that the write condition was added to
  //exec state's constraints before solveForBindings was called.
  //Todo:  Make this simpler and less prone to misuse.

  //ABH: Should be able to just add in the expr via cm.addConstraint ?
  //Todo: Double check
  klee::ConstraintManager cm(ExecStatePtr->constraints);
  cm.addConstraint(expr);

  klee::Query query(cm, klee::ConstantExpr::alloc(0, klee::Expr::Bool));

  solver->getInitialValues(query, arrays, initial_values);

  klee::ref<klee::Expr> value_disjunction
    = klee::ConstantExpr::alloc(0, klee::Expr::Bool);

  for (unsigned i=0; i<arrays.size(); ++i) {
    for (unsigned j=0; j<initial_values[i].size(); ++j) {

      klee::ref<klee::Expr> read =
	klee::ReadExpr::create(klee::UpdateList(arrays[i], 0),
			       klee::ConstantExpr::create(j, klee::Expr::Int32));

      klee::ref<klee::Expr> neq_expr =
	klee::NotExpr::create(
			      klee::EqExpr::create(read,
						   klee::ConstantExpr::create(initial_values[i][j], klee::Expr::Int8)));

      value_disjunction = klee::OrExpr::create(value_disjunction, neq_expr);
    }
  }

  // This may be a null-op how this interaction works needs to be better
  // understood
  value_disjunction = cm.simplifyExpr(value_disjunction);

  if (value_disjunction->getKind() == klee::Expr::Constant
      && cast<klee::ConstantExpr>(value_disjunction)->isFalse()) {
    addBindings(arrays, initial_values);
  } else {
    cm.addConstraint(value_disjunction);

    bool result;
    solver->mayBeTrue(klee::Query(cm,
				  klee::ConstantExpr::alloc(0, klee::Expr::Bool)), result);

    if (result) {
      printf("INVALID solver concretization!");
      std::exit(EXIT_FAILURE);
    } else {
      //TODO Test this path
      addBindings(arrays, initial_values);
    }
  }
}





//Take a list of constraints and their values, and a buffer.  Return size of serialization.

// |Header (magic)|| Header (# records) ||Rec1 name size X ||Rec1 val size Y||  Rec1 name   ||  Rec1 data   |
// |123 (uint8_t) ||<-    uint16_t    ->||<-   uint16_t -> ||<-  uint16_t ->||<- X  bytes ->||<- Y  bytes ->|

//And so on for all records until..
//| Footer (magic) |
//| 210 (uint8_t)  |


bool debugSerial = true;

void CVAssignment::serializeAssignments(void * buf, int bufSize) {


  std::vector<const klee::Array *> objects;
  std::vector<std::vector<unsigned char> > values;

  for (std::map<const Array *, std::vector<unsigned char> >::iterator it = bindings.begin(); it != bindings.end(); it++) {
    objects.push_back(it->first);
    values.push_back(it->second);
  }
    
  if (objects.size() > 0xFFFFFFFF) {
    printf("ERROR: Too many constraints to serialize in TASE \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

  uint16_t assignments = (uint16_t) objects.size();

  if (debugSerial) {
    printf("Attempting to serialize %d assignments \n", assignments);
    std::cout.flush();
  }

  uint8_t * itrPtr = (uint8_t *)  buf;
  //Header (magic)
  *itrPtr = 123;
  itrPtr++;

  //Header (assignments)
  *(uint16_t *) itrPtr = assignments;
  itrPtr += 2;

  std::vector< std::vector<unsigned char> >::iterator valIt =
    values.begin();
  for (std::vector<const klee::Array*>::iterator it = objects.begin(),
	 ie = objects.end(); it != ie; ++it) {
    const klee::Array *os = *it;
    std::vector<unsigned char> &arr = *valIt;
    std::string nameStr = os->name;

    if (debugSerial) {
      printf("Attempting to serialize string %s \n", nameStr.c_str());
      std::cout.flush();
    }

    
    
    //Print Name Size-------------
    if (nameStr.size() > 0xFFFFFFFF) {
      printf("Serialization error -- variable name too large \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }
    uint16_t nameSize = nameStr.size();
    if (debugSerial) {
      printf(" string length is  %d \n", nameSize);
      std::cout.flush();
    }
    
    *(uint16_t *) itrPtr = nameSize;
    itrPtr += 2;

    //Print Data size-------------
    if (arr.size() > 0xFFFFFFFF) {
      printf("Serialization error -- variable too large \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }
    uint16_t dataSize = arr.size();
    if (debugSerial) {
      printf("Data size is %d \n", dataSize);
      std::cout.flush();
    }
    
    *(uint16_t *) itrPtr = dataSize;
    itrPtr += 2;

    //Print Name------------------
    memcpy((void *) itrPtr, (void *) &nameStr[0] , nameSize);
    itrPtr += nameSize;

    //Print Data------------------
    for (int i = 0; i < dataSize; i++) {
      if (debugSerial) {
	printf("Data is %d \n", arr[i]);
	std::cout.flush();
      }
      *itrPtr = arr[i];
      itrPtr++;
    }
    ++valIt;
  }

  //We had better be pointing at the footer by now
  *itrPtr = 210;
  
  if (((uint64_t) itrPtr - (uint64_t) buf) > bufSize) {
    printf("Error in serialization: overflowed buffer \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

  if (debugSerial) {
    printf("Finished serializing %d assignments \n", assignments );
    std::cout.flush();
  }
}



CVAssignment * deserializeAssignments ( void * buf, int bufSize, Executor * exec) {

  if (debugSerial) {
    printf("Attempting to deserialize multipass assignments from buffer at 0x%lx \n with size %d \n", (uint64_t) buf, bufSize);
    std::cout.flush();
  }

  //Check magic
  uint8_t * itrPtr = (uint8_t *) buf;
  uint8_t magic = *itrPtr;
  if (magic != 123) {
    printf("Error deserializing constraints -- magic tag not found \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  itrPtr++;

  //Get expected number of records
  uint16_t numRecords = *(uint16_t *) itrPtr;
  itrPtr += 2;

  if (debugSerial) {
    printf("Found %d assignments during deserialization \n", numRecords);
    std::cout.flush();
  }

  //Iterate through records
  std::vector<const klee::Array*> objects;
  std::vector< std::vector<unsigned char> > values;

  //Invariant: itrPtr points to the beginning of a record at top of loop
  for (int i = 0; i < numRecords; i++) {
    
    uint16_t nameSize = *(uint16_t *) itrPtr;
    itrPtr += 2;

    //Get Data Size
    uint16_t dataSize = *(uint16_t *) itrPtr;
    itrPtr += 2;

    //Get Name
    char * tmpBuf = (char *) malloc(nameSize + 1);
    for (int j = 0; j < nameSize; j++)
      tmpBuf[j] = *(itrPtr + j);
    tmpBuf[nameSize] = 0; //Null terminate the string

    std::string nameStr(tmpBuf);
    
    if (debugSerial) {
      printf("Attempting to deserialize multipass assignments for var %s \n", nameStr.c_str());
      std::cout.flush();
    }
    
    const  Array * arr =  exec->getArrayCache()->CreateArray(nameStr , (uint64_t) dataSize);
    itrPtr = itrPtr + nameSize;

    //Get Data
    std::vector<unsigned char> data;
    for (int k = 0; k < dataSize; k++) {
      unsigned char theByte = (unsigned char) (*(itrPtr + k)); 
      data.push_back(theByte);
      if (debugSerial) {
	printf("Data is %d \n", *(itrPtr + k));
	std::cout.flush();
      }
    }
    itrPtr = itrPtr + dataSize;

    //Add array and data
    objects.push_back(arr);
    values.push_back(data);

  }

  //Magic check
  if (*itrPtr != 210) {
    printf("Error in deserialization: couldn't find final magic value \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

  //Actually add bindings

  CVAssignment cva =  CVAssignment(objects,values);
  return &cva;


  if (debugSerial) {
    printf("Exiting deserialization \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
}

