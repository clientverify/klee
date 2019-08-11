#include <llvm/IR/Instruction.h>
#include <unordered_map>
#include <set>

#include "Memory.h"

namespace klee{
class KInstruction;
class MemoryObject;
struct StackFrame;

//Add destructors
struct TrackSkip{
  StackFrame* recording_sf;
  llvm::Function* skip_function;
#if 0
  TrackSkip();
  TrackSkip(const TrackSkip &s);
  ~TrackSkip();
#endif
};

class MemoryData{
public:
  void clear_all();
  bool mod_contains(const MemoryObject*);
  bool mod_insert(const MemoryObject* mo, const KInstruction* ki_modified);
  void mod_print();
  std::unordered_map<const MemoryObject*, const KInstruction*> mo_mods;
};

class Empirical_Mem_Mod {
public:
  typedef struct TrackSkip TrackSkip;
  typedef class  MemoryData MemoryData;
  //Functions:
  //Returns true when in recording mode, false ow.
  bool recording();

  //Do we want a pointer here?
  //Add a memory object that was modified during recording
  void add_mod_mo(const MemoryObject* mo, const KInstruction* ki_modified);

  void start_recording(StackFrame* sf);

  //want this to be connected to turn off recording
  void end_recording();

  Empirical_Mem_Mod();

  Empirical_Mem_Mod(Empirical_Mem_Mod*);

  ~Empirical_Mem_Mod();

  //Storing mo_mod
  //Assuming MemoryObjects are not copied or reallocated
  //in different ExecutionStates.
  //The MemoryObject is the modified memory, and the
  //llvm::Instruction is the first instructiont that modified
  //during this function call.
  //Make this private after debugging, currently need it for asserts.
  MemoryData md;

private:
  bool record_mode;
  void print_mod_mo();
};

}
