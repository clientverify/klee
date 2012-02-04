//===-- EditDistance.h ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EDITDISTANCH_H
#define CLIVER_EDITDISTANCH_H

namespace cliver {

enum { MATCH=0, INSERT, DELETE };

template<class SequenceType, class ValueType>
class Score {
 public:

  Score()
    : match_cost_(0), 
      delete_cost_(1), 
      insert_cost_(1) {}

  Score(ValueType match_cost, ValueType delete_cost, ValueType insert_cost)
    : match_cost_(match_cost), 
      delete_cost_(delete_cost), 
      insert_cost_(insert_cost) {}

  ValueType match(SequenceType s1, SequenceType s2, 
                  unsigned pos1, unsigned pos2) {
    if (s1[pos1] == s2[pos2])
      return 0;
    return match_cost_;
  }

  ValueType insert(SequenceType s1, SequenceType s2, 
                  unsigned pos1, unsigned pos2) {
    return insert_cost_;
  }

  ValueType del(SequenceType s1, SequenceType s2, 
                  unsigned pos1, unsigned pos2) {
    return delete_cost_;
  }

 private:
  ValueType match_cost_, delete_cost_, insert_cost_;

};

template<class ScoreType, class SequenceType, class ValueType>
class EditDistanceTable {
 public:
  EditDistanceTable();

  void update_row(const SequenceType &s1, const SequenceType &s2,
                  unsigned row) {
    ValueType opt[3];

    for (int j=1; j<s2.size(); ++j) {
      opt[MATCH]  = cost(row-1, j-1) + score_.match(s1, s2, row, j);
      opt[INSERT] = cost(row, j-1) + score_.insert(s1, s2, row, j);
      opt[DELETE] = cost(row-1, j) + score_.del(s1, s2, row, j);
      for (int k=MATCH; k<=DELETE; ++k) {
        if (opt[k] < this->cost(row, j)) {
          this->set_cost(row, j, k, opt[k]);
        }
      }
    }
  }

  void compute_editdistance(const SequenceType& s1, const SequenceType s2) {
    for (int i=1; i<s1.size(); ++i) {
      update_row(s1, s2, i);
    }
  }

  ValueType cost(unsigned i, unsigned j) {
    return costs_[i];
  }

 private:
  SequenceType s1_, s2_;
  std::vector<ValueType> costs_;
  ScoreType score_;
};

} // end namespace cliver

#endif // CLIVER_EDITDISTANCH_H
