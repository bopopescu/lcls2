
//-----------------------------

//#include "psalgos/Types.h" // TwoIndexes
#include "../include/LocalExtrema.h"
#include <sstream>   // for stringstream
//#include <cmath>     // floor, ceil
//#include <iomanip>   // for std::setw

//-----------------------------

//typedef psalgos::types::TwoIndexes TwoIndexes;
//typedef psalgos::localextrema::TwoIndexes TwoIndexes;

using namespace std;

//-----------------------------

namespace localextrema {

//-----------------------------

std::vector<TwoIndexes> evaluateDiagIndexes(const size_t& rank)
{
  //MsgLog(_name(), debug, "in evaluateDiagIndexes, rank=" << rank);

  std::vector<TwoIndexes> v_inddiag;

  int indmax =  rank;
  int indmin = -rank;
  unsigned npixmax = (2*rank+1)*(2*rank+1);
  if(v_inddiag.capacity() < npixmax) v_inddiag.reserve(npixmax);
  v_inddiag.clear();

  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {

      // use rectangular region of radius = rank
      // remove already tested central row and column
      if (i==0 || j==0) continue;
      // use ring region (if un-commented)
      //if (m_rank>2 && floor(std::sqrt(float(i*i + j*j)))>(int)m_rank) continue;
      //TwoIndexes inds = {i,j};
      TwoIndexes inds(i,j);
      v_inddiag.push_back(inds);
    }
  }

  //if(m_pbits & 2) printMatrixOfDiagIndexes();

  return v_inddiag;
}

Vector<TwoIndexes>* evaluateDiagIndexes_drp(const size_t& rank, const bool drp, uint8_t*& drpPtr) { // TODO: remove bool drp 

  int indmax =  rank;
  int indmin = -rank;
  const unsigned npixmax = (2*rank+1)*(2*rank+1);

  Vector<TwoIndexes>* ptr = new(drpPtr) Vector<TwoIndexes>();
  drpPtr += sizeof(Vector<TwoIndexes>);
  if(ptr->capacity < npixmax) ptr->capacity = npixmax;

  int counter = 0;
  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {

      // use rectangular region of radius = rank
      // remove already tested central row and column
      if (i==0 || j==0) continue;
      
      ptr->data[counter++] = new(drpPtr) TwoIndexes(i,j);
      drpPtr += sizeof(TwoIndexes);
    }
  }
  ptr->len = counter;
  return ptr;
}

TwoIndexes** evaluateDiagIndexes_c(const size_t& rank, const bool drp, uint8_t*& drpPtr, int& len) {

  std::vector<TwoIndexes> v_inddiag;

  int indmax =  rank;
  int indmin = -rank;
  const unsigned npixmax = (2*rank+1)*(2*rank+1);

  if(v_inddiag.capacity() < npixmax) v_inddiag.reserve(npixmax);
  v_inddiag.clear();

  len = npixmax;
  TwoIndexes** team = new(drpPtr) TwoIndexes*[npixmax];
  drpPtr += sizeof(TwoIndexes) * npixmax;

  int counter = 0;
  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {

      // use rectangular region of radius = rank
      // remove already tested central row and column
      if (i==0 || j==0) continue;
      // use ring region (if un-commented)
      //if (m_rank>2 && floor(std::sqrt(float(i*i + j*j)))>(int)m_rank) continue;
      //TwoIndexes inds = {i,j};
      TwoIndexes inds(i,j);
      v_inddiag.push_back(inds);

      team[counter] = new(drpPtr) TwoIndexes(i,j);
      drpPtr += sizeof(TwoIndexes);

      //std::cout << "i,j: " << i << " " << j << std::endl;

      //v_inddiag_c->set(counter, std::addressof(ii));
      //std::cout << "ptr: " << ptr[counter] << std::endl;
      counter++;
    }
  }
  //std::cout << "v_inddiag_c.data: " << v_inddiag_c->operator()(1,2) << std::endl;

  //if(m_pbits & 2) printMatrixOfDiagIndexes();

  return team;
}

//-----------------------------

void printMatrixOfDiagIndexes(const size_t& rank)
{
  int indmax = rank;
  int indmin =-rank;

  std::stringstream ss; 
  ss << "In printMatrixOfDiagIndexes, rank=" << rank << '\n';

  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {
      int status = 1;
      if (i==0 || j==0) status = 0;
      //if (m_rank>2 && floor(std::sqrt(float(i*i + j*j)))>(int)m_rank) status = 0;
      if (i==0 && j==0) ss << " +";
      else              ss << " " << status;
    }
    ss << '\n';
  }

  //MsgLog(_name(), info, ss.str());
  cout << ss.str();
}

//-----------------------------

void printVectorOfDiagIndexes(const size_t& rank)
{
  vector<TwoIndexes> v_inddiag = evaluateDiagIndexes(rank);

  std::stringstream ss; 
  ss << "In printVectorOfDiagIndexes:\n Vector size: " << v_inddiag.size() << '\n';
  int n_pairs_in_line=0;
  for( vector<TwoIndexes>::const_iterator ij  = v_inddiag.begin();
                                          ij != v_inddiag.end(); ij++ ) {
    ss << " (" << ij->i << "," << ij->j << ")";
    if ( ++n_pairs_in_line > 9 ) {ss << "\n"; n_pairs_in_line=0;}
  }   
  ss << '\n';
  //MsgLog(_name(), info, ss.str());
  cout << ss.str();
}

//-----------------------------

size_t numberOfExtrema(const extrim_t *map, const size_t& rows, const size_t& cols, const extrim_t& vsel)
{
  size_t counter=0;
  for (size_t i=0; i<rows*cols; ++i) if(map[i]==vsel) counter++;
  return counter;
}

//-----------------------------

std::vector<TwoIndexes> vectorOfExtremeIndexes(const extrim_t *map, const size_t& rows, const size_t& cols, const extrim_t& vsel, const size_t& maxlen)
{
  std::vector<TwoIndexes> v;
  size_t _maxlen = (maxlen) ? maxlen : rows*cols/4;
  if(v.capacity() < _maxlen) v.reserve(_maxlen);
  v.clear();
  size_t irc=0;
  for (size_t r=0; r<rows; ++r) {
    for (size_t c=0; c<cols; ++c) {
      irc = r*cols+c;
      if(map[irc]==vsel) v.push_back(TwoIndexes(r,c));
    }
  }
  return v;
}

Vector<TwoIndexes>* vectorOfExtremeIndexes(const extrim_t *map, const size_t& rows, const size_t& cols, const extrim_t& vsel, const size_t& maxlen, const bool drp, uint8_t*& drpPtr)
{
  size_t _maxlen = (maxlen) ? maxlen : rows*cols/4;

  Vector<TwoIndexes> *ptr = new(drpPtr) Vector<TwoIndexes>();
  drpPtr += sizeof(Vector<TwoIndexes>);
  if(ptr->capacity < _maxlen) ptr->capacity = _maxlen;

  int counter = 0;
  size_t irc=0;
  for (size_t r=0; r<rows; ++r) {
    for (size_t c=0; c<cols; ++c) {
      irc = r*cols+c;
      if(map[irc]==vsel) {
        ptr->data[counter++] = new(drpPtr) TwoIndexes(r,c);
        drpPtr += sizeof(TwoIndexes);
      }
    }
  }
  ptr->len = counter;
  return ptr;
}

TwoIndexes** vectorOfExtremeIndexes(const extrim_t *map, const size_t& rows, const size_t& cols, const extrim_t& vsel, const size_t& maxlen, const bool drp, uint8_t*& drpPtr, int& len)
{
  //std::vector<TwoIndexes> v;
  size_t _maxlen = (maxlen) ? maxlen : rows*cols/4;
  //if(v.capacity() < _maxlen) v.reserve(_maxlen);
  //v.clear();

  len = _maxlen;
  TwoIndexes** team = new(drpPtr) TwoIndexes*[len];
  drpPtr += 10240;//sizeof(TwoIndexes) * len;

  int counter = 0;
  size_t irc=0;
  for (size_t r=0; r<rows; ++r) {
    for (size_t c=0; c<cols; ++c) {
      irc = r*cols+c;
      if(map[irc]==vsel) {
        //v.push_back(TwoIndexes(r,c));
        team[counter] = new(drpPtr) TwoIndexes(r,c);
        drpPtr += sizeof(TwoIndexes);
        counter++;
      }
    }
  }
  return team;
}
//-----------------------------
//-----------------------------
//-----------------------------
//-----------------------------

} // namespace localextrema

//-----------------------------
