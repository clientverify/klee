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
    : match_cost_(1), 
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
  EditDistanceTable(SequenceType s, SequenceType t) 
   : s_(s), t_(t), 
     s_size_(s.size()), t_size_(t.size()),
     m_(s_size_+1), n_(t_size_+1) {

    costs_ = new std::vector<ValueType>(m_*n_, 0);

    for (int i=0; i<m_; ++i) {
      set_cost(i, 0, 0, i);
    }
    for (int j=0; j<n_; ++j) {
      set_cost(0, j, 0, j);
    }
  }

  ~EditDistanceTable() {
    delete costs_;
  }

  void update_row(const SequenceType &s, const SequenceType &t, unsigned j) {
    ValueType opt[3];

    for (int i=1; i<m_; ++i) {
      int s_pos=i-1, t_pos=j-1;

      opt[MATCH]  = cost(i-1, j-1) + score_.match(s, t, s_pos, t_pos);
      opt[INSERT] = cost(  i, j-1) + score_.insert(s, t, s_pos, t_pos);
      opt[DELETE] = cost(i-1,   j) + score_.del(s, t, s_pos, t_pos);

      set_cost(i, j, MATCH, opt[MATCH]);

      for (int k=MATCH; k<=DELETE; ++k) {
        if (opt[k] < cost(i, j)) {
          set_cost(i, j, k, opt[k]);
        }
      }
    }
  }

  void compute_editdistance() {
    for (int j=1; j<n_; ++j) {
      update_row(s_, t_, j);
    }
  }

  void set_cost(unsigned i, unsigned j, unsigned k, ValueType cost) {
    (*costs_)[i + j*m_] = cost;
  }


  ValueType cost(unsigned i, unsigned j) const {
    return (*costs_)[i + j*m_];
  }

  const std::vector<ValueType>& costs() const { return *costs_; }

  void debug_print(std::ostream& os) const {

    int fwidth;
    os << " ";
    for (int i=0; i<m_; ++i) {
      fwidth = os.width(4);
      if (i == 0)
        os << "#";
      else
        os << s_[i-1];
      os.width(fwidth);
    }

    os << "\n";

    for (int j=0; j<n_; j++) {
      if (j == 0)
        os << "#";
      else
        os << t_[j-1];
      debug_print_cost_row(os, j);
      os << "\n";
    }
  }

  void debug_print_cost_row(std::ostream& os, int j) const {
    for (int i=0; i<m_; ++i) {
      int fwidth = os.width(4);
      os << cost(i,j);
      os.width(fwidth);
    }
  }

 protected:
  EditDistanceTable() {}

  SequenceType s_, t_;
  int s_size_, t_size_;
  int m_, n_;
  std::vector<ValueType>* costs_;
  ScoreType score_;
};

template<class ScoreType, class SequenceType, class ValueType>
std::ostream& operator<<(std::ostream& os, 
  const EditDistanceTable< ScoreType, SequenceType, ValueType > &edt) {
  edt.debug_print(os);
  return os;
}

} // end namespace cliver

#endif // CLIVER_EDITDISTANCH_H
