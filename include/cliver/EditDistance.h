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

#include <cmath>
#include <algorithm>

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

  inline ValueType match(const SequenceType &s1, const SequenceType &s2, 
                  unsigned pos1, unsigned pos2) {
    if (s1[pos1] == s2[pos2])
      return 0;
    return match_cost_;
  }

  inline ValueType insert(const SequenceType &s1, const SequenceType &s2, 
                  unsigned pos1, unsigned pos2) {
    return insert_cost_;
  }

  inline ValueType del(const SequenceType &s1, const SequenceType &s2, 
                  unsigned pos1, unsigned pos2) {
    return delete_cost_;
  }

 private:
  ValueType match_cost_, delete_cost_, insert_cost_;

};

template<class ScoreType, class SequenceType, class ValueType>
class EditDistanceTable {
 public:
  EditDistanceTable(const SequenceType &s, const SequenceType &t) 
   : s_(s),
     t_(t),
     m_(s_.size()+1), 
     n_(t_.size()+1),
     costs_(0) {}

  ~EditDistanceTable() {
    delete costs_;
  }

  inline void update_row(const SequenceType &s, const SequenceType &t, 
                         unsigned j) {
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

  ValueType compute_editdistance() {
    costs_ = new std::vector<ValueType>(m_*n_, 0);
    for (int i=0; i<m_; ++i) {
      set_cost(i, 0, 0, i);
    }
    for (int j=0; j<n_; ++j) {
      set_cost(0, j, 0, j);
    }
    for (int j=1; j<n_; ++j) {
      update_row(s_, t_, j);
    }
    return cost(m_-1, n_-1);
  }

  inline void set_cost(unsigned i, unsigned j, unsigned k, ValueType cost) {
    (*costs_)[i + j*m_] = cost;
  }

  inline ValueType cost(unsigned i, unsigned j) const {
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

  const SequenceType &s_;
  const SequenceType &t_;
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

////////////////////////////////////////////////////////////////////////////////

template<class ScoreType, class SequenceType, class ValueType>
class EditDistanceRow {
 public:
  EditDistanceRow(const SequenceType &s, const SequenceType &t) 
   : s_(s.size() > t.size() ? s : t), 
     t_(s.size() > t.size() ? t : s), 
     m_(s_.size()+1), 
     n_(t_.size()+1),
     costs_(0) {}

  ~EditDistanceRow() {
    delete costs_;
  }

  inline void update_row(const SequenceType &s, const SequenceType &t, 
                         unsigned j) {
    ValueType opt[3];
    set_cost(0, j, 0, j);

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

  ValueType compute_editdistance() {
    costs_ = new std::vector<ValueType>(m_*2, 0);
    for (int i=0; i<m_; ++i) {
      set_cost(i, 0, 0, i);
    }
    for (int j=1; j<n_; ++j) {
      update_row(s_, t_, j);
    }
    return cost(m_-1, n_-1);
  }

  inline void set_cost(unsigned i, unsigned j, unsigned k, ValueType cost) {
    (*costs_)[i + (j % 2)*m_] = cost;
  }

  inline ValueType cost(unsigned i, unsigned j) const {
    return (*costs_)[i + (j % 2)*m_];
  }

  const std::vector<ValueType>& costs() const { return *costs_; }

 protected:
  EditDistanceRow() {}
  const SequenceType &s_;
  const SequenceType &t_;
  int m_, n_;
  std::vector<ValueType>* costs_;
  ScoreType score_;
};

template<class ScoreType, class SequenceType, class ValueType>
std::ostream& operator<<(std::ostream& os, 
  const EditDistanceRow< ScoreType, SequenceType, ValueType > &edt) {
  edt.debug_print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

#define UDIM 10

template<class ScoreType, class SequenceType, class ValueType>
class EditDistanceUkkonen {
 public:
  EditDistanceUkkonen(const SequenceType &s, const SequenceType &t) 
   : s_(s.size() > t.size() ? s : t), 
     t_(s.size() > t.size() ? t : s), 
     m_(s_.size()+1), 
     n_(t_.size()+1) {}

  ~EditDistanceUkkonen() {
  }

  inline ValueType U(int i, int j) const {
    return U_v[i+(UDIM/2)][j];
  }

  inline bool computed_U(int i, int j) const {
    return U_c[i+(UDIM/2)][j];
  }

  inline void set_U(int i, int j, ValueType v) {
    //std::cout << "set_U(" << i << ", " << j << ") = " << v << "\n";
    U_c[i+(UDIM/2)][j] = true;
    U_v[i+(UDIM/2)][j] = v;
  }

  ValueType Ukkonen(int ab, int d) {
    ValueType dist;
    if (std::abs(ab) > d) {
      return -INT_MAX;
    }

    if (computed_U(ab,d)) {
      ValueType ret = U(ab, d);
      return ret;
    }

    dist = std::max(Ukkonen(ab+1, d-1), 
                    Ukkonen(ab,   d-1) + 1);
    dist = std::max(dist,
                    Ukkonen(ab-1, d-1) + 1);

    while (dist < s_.size() && (dist-ab) < t_.size() &&
           (score_.match(s_, t_, dist, dist-ab) == 0)) {
      //std::cout << "Incremented distance! " 
      //   << s_[dist] << ", " << t_[dist-ab] << ", dist: "<< dist+1 << "\n";
      dist++;
    }

    set_U(ab, d, dist);
    return dist;
  }

  // As = s, Bs = t
  ValueType compute_editdistance() {

    for (int i=0; i<UDIM; i++)
      for (int j=0; j<UDIM; j++)
        U_v[i][j] = -INT_MAX;
 
    for (int i=0; i<UDIM; i++)
      for (int j=0; j<UDIM; j++)
        U_c[i][j] = false;       

    // U[0,0] = max i s.t. As[1...i] = Bs[1...i]
    int i = 1;
    while (i < t_.size() && score_.match(s_, t_, i-1, i-1) == 0)
      ++i;

    set_U(0, 0, i-1);

    ValueType edit_cost = 0;
    //std::cout << "\n";

    int s_size = s_.size();
    //std::cout << "Ukkonen(" << s_.size()-t_.size() << ", " << edit_cost << ")\n";
    while (Ukkonen(s_.size()-t_.size(), edit_cost) < s_size) {
      //debug_print(std::cout);
      //std::cout << "\n";
      edit_cost++;
      //std::cout << "Ukkonen(" << s_.size()-t_.size() << ", " << edit_cost << ")\n";
    }
    //debug_print(std::cout);

    return edit_cost;
  }

  void debug_print(std::ostream& os) const {
    int fwidth;
    for (int i=0; i<UDIM; ++i) {
      for (int j=0; j<UDIM; j++) {
        int fwidth = os.width(4);
        if (U_v[i][j] == -INT_MAX)
          os << ".";
        else
          os << U_v[i][j];
        os.width(fwidth);
      }
      os << "\n";
    }
  }

 protected:
  EditDistanceUkkonen() {}
  const SequenceType &s_;
  const SequenceType &t_;
  int m_, n_;
  ValueType U_v[UDIM][UDIM];
  ValueType U_c[UDIM][UDIM];
  ScoreType score_;
};

template<class ScoreType, class SequenceType, class ValueType>
std::ostream& operator<<(std::ostream& os, 
  const EditDistanceUkkonen< ScoreType, SequenceType, ValueType > &edt) {
  edt.debug_print(os);
  return os;
}


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_EDITDISTANCH_H
