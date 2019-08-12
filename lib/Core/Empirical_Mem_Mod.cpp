
//#include "Empirical_Mem_Mod.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "Memory.h"
#include "klee/ExecutionState.h"

using namespace llvm;
using namespace klee;
using namespace std;

Empirical_Mem_Mod::TrackSkip  ts;
Empirical_Mem_Mod::MemoryData md;

void MemoryData::clear_all() {
  mo_mods.clear();
  //TODO: clear the allocs and frees too!
}

bool MemoryData::mod_contains(const MemoryObject* mo) {
  if(mo_mods.find(mo) != mo_mods.end())
      return true;
  return false;
}

bool MemoryData::mod_insert(const MemoryObject* mo, const KInstruction* ki_modified) {
  assert(mo != nullptr && ki_modified != nullptr);
  assert(mo->record_me);
  //Already in map.
  if(mod_contains(mo))
    return false;

  std::pair<const MemoryObject*, const KInstruction*> p(mo, ki_modified);
  mo_mods.insert(p);
  return true;
}

//Functions:
//Returns true when in recording mode, false ow.
bool Empirical_Mem_Mod::recording(){ return record_mode; }

//Turns on recording on call to function we want to skip
void Empirical_Mem_Mod::start_recording(StackFrame* sf) {
  cout << "Empirical_Mem_Mod::start_recording\n";
  assert(record_mode == false);
  assert(ts.recording_sf == nullptr);

  record_mode = true;
  ts.recording_sf = sf;
}

//Turns off recording on fuction we're skipping. Called on return.
void Empirical_Mem_Mod::end_recording() {
  std::cout << "Empirical_Mem_Mod::end_record(): #MO modified: "
            << md.mo_mods.size() << "\n";
  record_mode = false;

  //Assuming this doesn't delete the elements.
  md.clear_all();
  ts.recording_sf = nullptr;
  ts.skip_function = nullptr;
}

//Do we want a pointer here?
//Records MemoryObject modified during execution of skipped function.
void Empirical_Mem_Mod::add_mod_mo(const MemoryObject* mo, const KInstruction* ki_modified) {
  assert(record_mode);

  if(md.mod_insert(mo, ki_modified)){
    std::string alloc_info;
    mo->getAllocInfo(alloc_info);
    cout << "Empirical_Mem_Mod::add_mod_mo:"
      << " MemoryObject info: "
      << "size " << mo->size << " " << alloc_info
      << "\n\tModified at: " << *ki_modified << "\n";
  }

}

void MemoryData::mod_print() {
  std::cout << "Empirical_Mem_Mod::mod_print";
  for (unordered_map<const MemoryObject*, const KInstruction*>::iterator
      it = mo_mods.begin(); it != mo_mods.end(); ++it){
    std::string alloc_info;
    const MemoryObject* mo = it->first;
    const KInstruction* i = it->second;
    mo->getAllocInfo(alloc_info);
    std::cout<< "\n";
    std::cout << "\tMemoryObject info: object at " << mo->address
      << " of size " << mo->size << " " << alloc_info
      << " Modified at: " << *i;
  }
  std::cout<< "\n";
}

Empirical_Mem_Mod::Empirical_Mem_Mod()
    : record_mode(false) {
  cout << "Empirical_Mem_Mod::Empirical_Mem_Mod()\n";
  //Does this need to be initalized somehow?
  assert(md.mo_mods.empty()); //remove later?
  assert(ts.recording_sf  == nullptr);
  assert(ts.skip_function == nullptr);
}

Empirical_Mem_Mod::Empirical_Mem_Mod(Empirical_Mem_Mod* m)
  : record_mode(m->record_mode),
    md(m->md) {
  cout << "Empirical_Mem_Mod::Empirical_Mem_Mod(Empirical_Mem_Mod* m)\n";
  assert(ts.recording_sf  == nullptr);
  assert(ts.skip_function == nullptr);
}

Empirical_Mem_Mod::~Empirical_Mem_Mod() {
  cout << "Empirical_Mem_Mod::~Empirical_Mem_Mod()\n";
  end_recording();
  md.clear_all();
}


