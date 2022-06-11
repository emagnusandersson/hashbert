#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h> // check if directory exist
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strncpy
#include <libgen.h> // basename
#include <signal.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <filesystem> // fs::is_directory
#include <vector>
#include <algorithm>    // std::sort, std::find
#include <string>       // std::to_string
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <thread> //sleep_for
#include <errno.h>
#include <cerrno>
//#include <format>
//#include <experimental/filesystem> 

#if defined(__APPLE__)
#  define COMMON_DIGEST_FOR_OPENSSL
#  include <CommonCrypto/CommonDigest.h>
#  define SHA1 CC_SHA1
#else
#  include <openssl/md5.h>
#endif

#define __debugbreak raise(SIGTRAP);

#define LENFILENAME 1024

//namespace fs = std::filesystem;

//
// Compile with:
//   g++ -O3 -o hashbert hashbert.cc -lcrypto -std=gnu++20
// Compile for gdb:
//   g++ -g -o hashbert hashbert.cc -lcrypto -std=gnu++20

// sudo cp hashbert /usr/local/bin/

// 
// How to debug (typical debug session):
//

// gdb hashbert
// b 209          // set breakpoint
// r [arguments]  // run 
// l              // List the code
// p [variable]   // Print variable
// finish         // Step out of function
// i b   // list breakpoints

// cd /run/media/magnus/myPassport/
// gdb hashbert

// Todo: dry run option

  // Google for "ansi vt100 codes" to learn about the codes in the printf-strings
#define ANSI_CURSOR_SAVE        "\0337"
#define ANSI_CURSOR_RESTORE     "\0338"
#define ANSI_FONT_CLEAR         "\033[0m"
#define ANSI_FONT_BOLD          "\033[1m"
#define ANSI_CLEAR_BELOW        "\033[J"
#define ANSI_CURSOR_UP(n)       "\033[" #n "A"
#define ANSI_CURSOR_DOWN(n)     "\033[" #n "B"
#define ANSI_CLEAR_RIGHT        "\033[K"
#define ANSI_CURSORDN           "\033[B"

#define MAKESPACE_N_SAVE "\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE
#define MY_RESET ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW
//#define MAKESPACE_N_SAVE "\n\n\033[2A\0337";
//const char* MAKESPACE_N_SAVE={"\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE};


template<typename ... Args>
char* mysprintf(const char* fmt, Args ... args){  // mysprintf: like snprintf but with fewer arguments.
  const int L_STRTMP=300;
  static char strTmp[L_STRTMP];
  auto intL=snprintf(strTmp, L_STRTMP, fmt, args ...);
  return strTmp;
}




// struct returnType_myRealPath{
//   char* strErr;
//   char* buf;
// };
// returnType_myRealPath myRealPath(const char* str){
//     char buf[PATH_MAX]; /* PATH_MAX incudes the \0 so +1 is not required */
//     char *res = realpath(str, buf);
//     if(res) {  return {NULL, buf};  }
//     else{ return {strerror(errno), buf}; }
// }
struct returnType_myRealPath{
  std::string strErr;
  std::string buf;
};
returnType_myRealPath myRealPath(const std::string str){
    char buf[PATH_MAX]; /* PATH_MAX incudes the \0 so +1 is not required */
    char *res = realpath(str.c_str(), buf);
    if(res) {  return {"", buf};  }
    else{ return {strerror(errno), buf}; }
}

#define L_BUFFER 1024
//void calcHashOfFile(std::ifstream& fin, char out[33]) {
void calcHashOfFile(std::ifstream& fin, std::string& out) {
  int i, nRead;
  static const char* digits = "0123456789abcdef";
  MD5_CTX ctx;
  unsigned char digest[16];
  
  unsigned char data[L_BUFFER];
  
  MD5_Init(&ctx);

  while(!fin.eof()) {
      //fin.read(buffer.data(), buffer.size());
      fin.read((char*)data, L_BUFFER);
      std::streamsize nRead=fin.gcount();
      MD5_Update (&ctx, data, nRead);
  }

  MD5_Final(digest, &ctx);
  for (i = 0; i < 16; ++i) {
    //snprintf(&(out[i*2]), 3, "%02x", (unsigned int)digest[i]);  // Each "write" will write 3 bytes (2 + 1 null)
    out[i*2]=digits[(digest[i] & 0xf0)>>4];
    out[i*2+1]=digits[digest[i] & 0x0f];
  }
}


unsigned int nFile=0; // Keeping track of how many files are found

std::chrono::steady_clock::time_point tStart = std::chrono::steady_clock::now();
void toc(int &nHour, int &nMin, int &nSec){
  std::chrono::steady_clock::time_point tNow= std::chrono::steady_clock::now();
  int tDur=std::chrono::duration_cast<std::chrono::seconds> (tNow-tStart).count();
  nSec=tDur%60;
  nMin=tDur/60;
  nHour=(nMin/60)%60; nMin=nMin%60;
}
//char strElapsedTime[]="%u:%02u:%02u";
  
char strFmtParseTree[]="(" ANSI_FONT_BOLD "t" ANSI_FONT_CLEAR ": %u:%02u:%02u, " 
ANSI_FONT_BOLD "n" ANSI_FONT_CLEAR ": %u)\n";

char strFmtSyncStatus[]="(" ANSI_FONT_BOLD "t" ANSI_FONT_CLEAR ": %u:%02u:%02u, " 
ANSI_FONT_BOLD "n" ANSI_FONT_CLEAR ": %u/%u, " 
ANSI_FONT_BOLD "touched" ANSI_FONT_CLEAR ": %u, " 
ANSI_FONT_BOLD "untouched" ANSI_FONT_CLEAR ": %u, " 
ANSI_FONT_BOLD "new" ANSI_FONT_CLEAR ": %u, " 
ANSI_FONT_BOLD "deleted" ANSI_FONT_CLEAR ": %u)\n";

struct RowTreeT{
  unsigned int intTime;
  uint64_t intSize;
  std::string strName;
};

//void writeFolderContentToHierarchyFile(FILE *fpt, const std::string strDir){  // Write folder content (that is files only) of strDir. For folders in strDir recursive calls are made.
//void writeFolderContentToHierarchyFile(std::fstream& fpt, const std::string strDir){  // Write folder content (that is files only) of strDir. For folders in strDir recursive calls are made.
//void writeFolderContentToHierarchyFile(std::fstream& fpt, const std::string fsDir, const std::string flDir){  // Write folder content (that is files only) of strDir. For folders in strDir recursive calls are made.
void parseTree(std::vector<RowTreeT>& arrTree, const std::string fsDir, const std::string flDir){  // Write folder content (that is files only) of strDir. For folders in strDir recursive calls are made.
  DIR *dir;
  struct dirent *entry;
  struct EntryT {
    std::string str;
    char boDir;
    EntryT(const std::string& s, const char c) : str(s), boDir(c) {}
    bool operator < (const EntryT& structA) const {
        return str.compare(structA.str)<0;
    }
  };
  
  std::vector<EntryT> vecEntry;
     
  //if (!(dir = opendir(strDir.c_str())))        return;
  if (!(dir = opendir(fsDir.c_str())))        return;
  
  while(1) {
    if (!(entry = readdir(dir))) break;
    vecEntry.push_back(EntryT(entry->d_name, entry->d_type == DT_DIR));
  }
  std::sort(std::begin(vecEntry), std::end(vecEntry));

  for(auto it=vecEntry.begin(); it!=vecEntry.end(); ++it) {
    std::string strPath;
    EntryT ent=*it;

    //strPath=strDir; 
    // if(strDir==".") strPath=ent.str;
    // else {strPath=strDir; strPath.append("/").append(ent.str); }
    std::string flPath;
    if(flDir.size()){ flPath=flDir+'/'+ent.str;} else{ flPath=ent.str;}
    auto fsPath=fsDir+'/'+ent.str;
    if (ent.boDir) {
      //if(strcmp(ent.str.c_str(), ".") == 0 || strcmp(ent.str.c_str(), "..") == 0)     continue;
      if(ent.str.compare(".") == 0 || ent.str.compare("..") == 0)     continue;
      //writeFolderContentToHierarchyFile(fpt, fsPath, flPath);
      parseTree(arrTree, fsPath, flPath);
    }
    else {
      struct stat st;
      //if(stat(strPath.c_str(), &st) != 0) perror(strPath.c_str());
      if(stat(fsPath.c_str(), &st) != 0) perror(fsPath.c_str());
      //fprintf(fpt, "%u %u %s\n", st.st_mtim.tv_sec, st.st_size, strPath.c_str());
      // fpt<<st.st_mtim.tv_sec
      // <<' '<<st.st_size
      // <<' '<<flPath.c_str()<<'\n';
      //RowTreeT rowTree={st.st_mtim.tv_sec, st.st_size, flPath.c_str()};
      //RowTreeT rowTree={.intTime=st.st_mtim.tv_sec, .intSize=st.st_size, .strName=flPath.c_str()};
      RowTreeT rowTree;
      rowTree.intTime=st.st_mtim.tv_sec; rowTree.intSize=st.st_size, rowTree.strName=flPath.c_str();
      arrTree.push_back(rowTree);
      nFile++;
      if(nFile%100==0){
        printf(MY_RESET);
        int nHour, nMin, nSec;    toc(nHour, nMin, nSec);
        printf(strFmtParseTree, nHour, nMin, nSec, nFile);
      }
    }
  } 
  closedir(dir);
}
struct RowHashFileT{
  std::string strHash;
  unsigned int intTime;
  uint64_t intSize;
  std::string strName;
};
bool funSortByStrName (std::string a, std::string b) { return (a<b); }
std::vector<RowHashFileT> parseHashcodeFile(std::fstream& fptOld){
  std::vector<RowHashFileT> arr;
  while(1) {
    std::string strHash;
    unsigned int intTime;
    uint64_t intSize;
    std::string strName;
    RowHashFileT row;

    fptOld>>row.strHash>>row.intTime>>row.intSize>>std::ws;
    std::getline(fptOld, row.strName);
    if(fptOld.fail()) 
      break;
    arr.push_back(row);
  }
  return arr;
}

//int mergeOldNHierarchy(FILE *fptNew, FILE *fptOld, FILE *fptHierarchy, int boRealRun){  // Merge fptOld and fptHierarchy to fptNew and recalculate hashcode 
//int mergeOldNHierarchy(std::ofstream& fptNew, std::ifstream& fptOld, std::fstream& fptHierarchy, const std::string fsDir, int boRealRun){  // Merge fptOld and fptHierarchy to fptNew and recalculate hashcode for new files and when file-mod-time/size is different
int mergeOldNHierarchy(std::ofstream& fptNew, std::ifstream& fptOld, std::vector<RowTreeT> arrTree, const std::string fsDir, int boRealRun){  // Merge fptOld and fptHierarchy to fptNew and recalculate hashcode for new files and when file-mod-time/size is different
  //char strHashOld[33], strHashCalc[33];
  //const char *pstrHash;
  std::string *pstrHash;
  char boOldEndReached=0, boHierarchyEndReached=0;
  char boMakeReadOld=1, boMakeReadHierarchy=1;
  unsigned int nRow=0, nUntouched=0, nTouched=0, nDelete=0, nNew=0;
  unsigned int intTimeHierarchy;
  uint64_t intSizeHierarchy;
  //char strFileOld[LENFILENAME], strFileHierarchy[LENFILENAME];

  std::string strHashCalc(32,0);
  std::string strFileHierarchy;

  //FILE *fptLog = fopen("log.txt", "w");
  std::vector<RowHashFileT> RowOld=parseHashcodeFile((std::fstream&) fptOld);
  //std::sort(std::begin(RowOld), std::end(RowOld), funSortByStrName);
  std:sort( RowOld.begin(), RowOld.end(), [ ]( const auto& lhs, const auto& rhs )
    {
      return lhs.strName < rhs.strName;
    });
    
  std::sort( arrTree.begin(), arrTree.end(), [ ]( const auto& lhs, const auto& rhs )
    {
      return lhs.strName < rhs.strName;
    });
  int iHierarchy=0, iOld=0;
    
  enum Status { DONE, DELETED, UNTOUCHED, TOUCHED, NEWFILE };
  enum Status myStatus;
  unsigned int intTimeOld;
  uint64_t intSizeOld;
  std::string strHashOld;
  std::string strFileOld;
  while(1) {
    int intCmp;
    if(boMakeReadOld) {
      boMakeReadOld=0; 
      //if(fscanf(fptOld, "%32s %u %u %1023[^\n]\n", strHashOld, &intTimeOld, &intSizeOld, strFileOld)!=4) boOldEndReached=1;

      // fptOld>>strHashOld>>intTimeOld>>intSizeOld>>std::ws;
      // std::getline(fptOld, strFileOld);
      // boOldEndReached=fptOld.fail();
      boOldEndReached=iOld>=RowOld.size();
      if(!boOldEndReached){
        //const auto [ strHashOld, intTimeOld, intSizeOld, strFileOld]=RowOld[iOld];
        RowHashFileT r=RowOld[iOld];
        strHashOld=r.strHash; intTimeOld=r.intTime; intSizeOld=r.intSize; strFileOld=r.strName;
        iOld++;
      }
    }
    if(boMakeReadHierarchy) {
      boMakeReadHierarchy=0;
      // if(fscanf(fptHierarchy, "%u %u %1023[^\n]\n", &intTimeHierarchy, &intSizeHierarchy, strFileHierarchy)!=3) boHierarchyEndReached=1;

      // fptHierarchy>>intTimeHierarchy>>intSizeHierarchy>>std::ws;
      // std::getline(fptHierarchy, strFileHierarchy);
      // boHierarchyEndReached=fptHierarchy.fail();

      boHierarchyEndReached=iHierarchy>=arrTree.size();
      if(!boHierarchyEndReached){
        //const auto [ strHashOld, intTimeOld, intSizeOld, strFileOld]=arrTree[iHierarchy];
        RowTreeT r=arrTree[iHierarchy];
        intTimeHierarchy=r.intTime; intSizeHierarchy=r.intSize; strFileHierarchy=r.strName;
        iHierarchy++;
      }
    }
    
    
    if(!boOldEndReached && !boHierarchyEndReached){
      intCmp=strFileOld.compare(strFileHierarchy);
      if(intCmp==0){
        if(intTimeOld==intTimeHierarchy && intSizeOld==intSizeHierarchy) { myStatus=UNTOUCHED; }
        else { myStatus=TOUCHED; }
        boMakeReadOld=1; boMakeReadHierarchy=1; nRow++;
      }
      else if(intCmp<0){ // The row exist in fptOld but not in fptHierarchy
        myStatus=DELETED; boMakeReadOld=1;
      }
      else if(intCmp>0){ // The row exist in fptHierarchy but not in fptOld
        myStatus=NEWFILE; boMakeReadHierarchy=1; nRow++;
      }
    }else if(!boOldEndReached){ // Ending (obsolete) row(s) in fptOld
      myStatus=DELETED; boMakeReadOld=1; 
    }else if(!boHierarchyEndReached){ // Ending (new) row(s) in fptHierarchy
      myStatus=NEWFILE; boMakeReadHierarchy=1; nRow++;
    }else { myStatus=DONE;  }
    
    
    if(myStatus==DONE){ 
      printf(ANSI_CURSOR_RESTORE ANSI_CURSOR_UP(1) ANSI_CLEAR_BELOW "Done.\n" ANSI_CURSOR_SAVE);
    } else {  printf(MY_RESET); }
        
    int nHour, nMin, nSec;    toc(nHour, nMin, nSec);
    //printf("(%u:%02u:%02u", nHour, nMin, nSec);
    //printf("File " ANSI_FONT_BOLD "%u/%u" ANSI_FONT_CLEAR ": " , nRow, nFile); 
    
    printf(strFmtSyncStatus, nHour, nMin, nSec, nRow, nFile, nTouched, nUntouched, nNew, nDelete);
    
    if(myStatus==DONE){  break; }
    else if(myStatus==DELETED){ nDelete++; }  //printf("File removed.\n");
    else if(myStatus==UNTOUCHED){ nUntouched++; pstrHash=&strHashOld; }  // printf("modTime-size match (Reusing hash).\n");  printf("\n");
    else if(myStatus==TOUCHED){ printf("Touched (Recalculating hash):\n"); nTouched++; pstrHash=&strHashCalc; }
    else if(myStatus==NEWFILE){ printf("New file (Calculating hash):\n");  nNew++; pstrHash=&strHashCalc; }
    
      // Write file name to screen. 
      // Only do this when the hash needs to be calculated (For prestanda reasons. (The file names will be flashing by to fast to read anyhow.))
    if(myStatus>=TOUCHED ){ printf("%s\n", strFileHierarchy.c_str());  }

      // Calculate hash
    if(myStatus>=TOUCHED ){
      if(boRealRun){
        auto fsName=fsDir+"/"+strFileHierarchy;
        std::ifstream fpt(fsName);
        if(fpt.fail()) {  perror((fsName).c_str());   return 1;  }
        calcHashOfFile(fpt, *pstrHash);

        //fprintf(fptLog, "%s %10u %10u %s\n", "pstrHash", intTimeHierarchy, intSizeHierarchy, strFileHierarchy); 
      }
    }
      // Write to temporary (new) HASHFILE
    if(myStatus>=UNTOUCHED){   
      //fprintf(fptNew, "%s %10u %10u %s\n", pstrHash, intTimeHierarchy, intSizeHierarchy, strFileHierarchy);
      fptNew<<*pstrHash
      <<' '<<std::setw(10)<<intTimeHierarchy
      <<' '<<std::setw(10)<<intSizeHierarchy
      <<' '<<strFileHierarchy<<'\n';
    }
  }
  
  return 0;
}

std::string join(const std::vector<std::string> &v,  const std::string c){
  std::string s;
  for(auto p=v.begin();  p!=v.end();  ++p) {
  //for(std::vector<std::string>::const_iterator p=v.begin();  p!=v.end();  ++p) {
    s+=*p;
    if(p!=v.end()-1) s+=c;
  }
  return s;
}
std::string getHighestMissing(std::string strFile){
  struct stat info;
  char *strPathC, *strPathCL;
  strPathCL=strdup(strFile.c_str());
  strPathC=strdup(strFile.c_str());
  while(1){
    strPathC=dirname(strPathC);
    if( stat( strPathC, &info ) == 0 ) { return strPathCL; } // if strPathC exist return strPathCL
    free(strPathCL);
    strPathCL=strdup(strPathC);
  }
}


std::string getHighestMissing1(std::string& strFile, int lCur){
  struct stat info;
  std::string strNew=strFile;
  int iStart=lCur-1;
  int lMissingLast=lCur; // (intMax)
  while(1){
    int iSlash = strFile.find_last_of('/', iStart);
    if(iSlash==std::string::npos) return ".";
    strNew[iSlash]=0; 
    bool boFound=stat( strNew.c_str(), &info ) == 0;
    strNew[iSlash]='/'; 
    if(boFound) {
      return strNew.substr(0,lMissingLast);
    }
    lMissingLast=iSlash;

  }
}



struct returnType_doesParentExist{
  int iSlash;
  bool boParentExist;
};
returnType_doesParentExist doesParentExist(std::string &strFile){
  struct stat info;

  int iSlash = strFile.find_last_of('/');
  if(iSlash==std::string::npos) return {iSlash,false};
  strFile[iSlash]=0;
  bool boParentExist= stat( strFile.c_str(), &info ) == 0;
  strFile[iSlash]='/';
  return {iSlash,boParentExist};
}


struct returnType_getSuitableTimeUnit{
  int v;
  char charUnit;
};
returnType_getSuitableTimeUnit getSuitableTimeUnit(const int t){ // t in seconds
  int tAbs=abs(t), tSign=t>=0?+1:-1;
  if(tAbs<=120) { return {tSign*tAbs, 's'};}
  tAbs/=60; // t in minutes
  if(tAbs<=120) { return {tSign*tAbs, 'm'};} 
  tAbs/=60; // t in hours
  if(tAbs<=48) { return {tSign*tAbs, 'h'};}
  tAbs/=24; // t in days
  if(tAbs<=2*365) { return {tSign*tAbs, 'd'};}
  tAbs/=365; // t in years
  return {tSign*tAbs, 'y'};
}





#define FMT_Check_Missing_File ANSI_FONT_BOLD "Row:" ANSI_FONT_CLEAR "%u, "     ANSI_FONT_BOLD "Missing file:" ANSI_FONT_CLEAR " %s\n"
#define FMT_Check_Missing_Folder ANSI_FONT_BOLD "Row:" ANSI_FONT_CLEAR "%u-%u (%u), "     ANSI_FONT_BOLD "Missing folder:" ANSI_FONT_CLEAR " %s\n"
#define FMT_Check_Missing_Files ANSI_FONT_BOLD "Row:" ANSI_FONT_CLEAR "%u-%u (%u), "     ANSI_FONT_BOLD "Missing files in:" ANSI_FONT_CLEAR " %s\n"




//void check(FILE *fpt){  // Go through the hashcode-file, for each file, check if the hashcode matches the actual files hashcode
//int check(std::string strFileHashOld, int intStart){  // Go through the hashcode-file, for each row (file), check if the hashcode matches the actual files 
std::string check(const char* cstrFileHashOld, int intStart){  // Go through the hashcode-file, for each row (file), check if the hashcode matches the actual files hashcode  
  //char strHash[33];
  std::string strHash(32,0);
  unsigned int iRowCount=0, nNotFound=0, nMisMatchTimeSize=0, nMisMatchHash=0, nOK=0;
  
  if(access(cstrFileHashOld, F_OK)==-1) { return strerror(errno);}
  //FILE *fptHashOld = fopen(cstrFileHashOld, "r");
  std::ifstream fptHashOld(cstrFileHashOld);

    
  printf(MAKESPACE_N_SAVE);
  
    // Variables for "missing streaks"
  std::string strMissingFile, strHighestFolderMissing;
  unsigned int nNotFoundLoc, iNotFoundFirst;
  int lenStrHighestFolderMissing;
  char charMissingL=' ';
  
  while(1) {
    int nHour, nMin, nSec;

    //char strFile[LENFILENAME], strHashOld[33];
    //char boGotRow=fscanf(fptHashOld,"%32s %u %u %1023[^\n]",strHashOld, &intTimeOld, &intSizeOld, strFile)  ==  4;
    
      // Read data
    std::string strFile, strHashOld;
    int intTimeOld;
    uint64_t intSizeOld;
    fptHashOld>>strHashOld>>intTimeOld>>intSizeOld>>std::ws;
    std::getline(fptHashOld, strFile);
    
      // Bail if finished
    if(fptHashOld.fail()){
      toc(nHour, nMin, nSec);
      printf(MY_RESET
      ANSI_FONT_BOLD "Time:" ANSI_FONT_CLEAR " %u:%02u:%02u, Done ("
      ANSI_FONT_BOLD "RowCount:" ANSI_FONT_CLEAR " %u, "
      ANSI_FONT_BOLD "NotFound:" ANSI_FONT_CLEAR " %u, "
      ANSI_FONT_BOLD "MisMatchTimeSize:" ANSI_FONT_CLEAR " %u, "
      ANSI_FONT_BOLD "MisMatchHash:" ANSI_FONT_CLEAR " %u, "
      ANSI_FONT_BOLD "OK:" ANSI_FONT_CLEAR " %u)\n", nHour, nMin, nSec, iRowCount, nNotFound, nMisMatchTimeSize, nMisMatchHash, nOK);
      break;
    }

    iRowCount++;

      // Continue if intStart hasn't been reached.
    if(iRowCount<intStart) { continue;}  // iRowCount / intStart (row number) is 1-indexed


    //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    //if(strFile=="0Downloads/0obs/Win_7_64Bit.iso") __debugbreak;

      // Check if one can bail to next ("continue") if we're in a known missing directory.
    if(charMissingL=='d'){
      if(strHighestFolderMissing.compare(0, std::string::npos, strFile, 0, lenStrHighestFolderMissing)==0){
        nNotFoundLoc++; nNotFound++; continue; 
      } else{ 
        if(nNotFoundLoc==1) printf(MY_RESET FMT_Check_Missing_File MAKESPACE_N_SAVE, iNotFoundFirst, strMissingFile.c_str());
        else printf(MY_RESET FMT_Check_Missing_Folder MAKESPACE_N_SAVE, iNotFoundFirst, iNotFoundFirst+nNotFoundLoc-1, nNotFoundLoc, strHighestFolderMissing.c_str());
        strHighestFolderMissing=""; lenStrHighestFolderMissing=0; charMissingL=' '; nNotFoundLoc=0;
      }
    }
    
    toc(nHour, nMin, nSec);
    printf(MY_RESET "%u:%02u:%02u, " ANSI_FONT_BOLD "Checking row:" ANSI_FONT_CLEAR " %u (%s)\n", nHour, nMin, nSec, iRowCount, strFile.c_str());


      // Open file.
    std::ifstream fpt(strFile);
    bool boFileFound=true;
    if(fpt.fail()){
      if(errno!=ENOENT) return strerror(errno);
      boFileFound=false;
    }

      // If file wasn't found.
    if(!boFileFound) {
      auto strPar=dirname(strdup(strFile.c_str()));
      struct stat info;
      bool boParExist=stat( strPar, &info )==0;
      char charMissing=boParExist ?'f':'d';
      if(charMissing=='f') {
        if(charMissing!=charMissingL) { // If first occurance
          charMissingL='f';  strMissingFile=strFile;
          //strLabel=dirname(strdup(strMissingFile.c_str()));
          iNotFoundFirst=iRowCount;  nNotFoundLoc=0;
        }
        nNotFound++; nNotFoundLoc++;
      }else{  // If charMissing=='d'
        if(charMissingL=='f'){ // If charMissing is switching from f to d.
          if(nNotFoundLoc==1) printf(MY_RESET FMT_Check_Missing_File MAKESPACE_N_SAVE, iNotFoundFirst, strMissingFile.c_str());
          else {
            char* strTmp=strdup(strMissingFile.c_str());
            printf(MY_RESET FMT_Check_Missing_Files MAKESPACE_N_SAVE, iNotFoundFirst, iNotFoundFirst+nNotFoundLoc-1, nNotFoundLoc, dirname(strTmp));
          }
        }
        if(charMissing!=charMissingL) { // If first occurance
          charMissingL='d';  strMissingFile=strFile;
          //strLabel=dirname(strdup(strMissingFile.c_str()));
          iNotFoundFirst=iRowCount;  nNotFoundLoc=0;

          std::string strTmp;
          if(strcmp(strPar,".")==0) strTmp=".";
          else{
            strTmp=getHighestMissing(strFile);
          }
          strHighestFolderMissing=strTmp;
          lenStrHighestFolderMissing=strHighestFolderMissing.length();
        }
        nNotFound++; nNotFoundLoc++;
      }
      continue;
    }
    
      // If missing-streak ended
    if(charMissingL=='f'){ // If file was missing, and now things are found.
      if(nNotFoundLoc==1) printf(MY_RESET FMT_Check_Missing_File MAKESPACE_N_SAVE, iNotFoundFirst, strMissingFile.c_str());
      else {
        char* strTmp=strdup(strMissingFile.c_str());
        printf(MY_RESET FMT_Check_Missing_Files MAKESPACE_N_SAVE, iNotFoundFirst, iNotFoundFirst+nNotFoundLoc-1, nNotFoundLoc, dirname(strTmp));
      }
      charMissingL=' '; nNotFoundLoc=0;
    }

      // Calculate hash
    calcHashOfFile(fpt, strHash);
    

    if(strHash.compare(strHashOld)){  // If hashes mismatches
      
        // Check modTime and size (perhaps the user forgott to run sync before running check
      struct stat st;
      if(stat(strFile.c_str(), &st) != 0) perror(strFile.c_str());
      int boTMatch=st.st_mtim.tv_sec==intTimeOld, boSizeMatch=st.st_size==intSizeOld;
      std::vector <std::string> StrTmp;
      if(!boTMatch || !boSizeMatch ){ // If meta data mismatches
        char* strTmp=strdup(strFile.c_str());
        std::string strBase=basename(strTmp);
        if(strBase.compare(cstrFileHashOld)==0) { 
          auto strTmp=mysprintf(MY_RESET ANSI_FONT_BOLD "Row:" ANSI_FONT_CLEAR "%u, (%s is ignored)", iRowCount, strFile.c_str());
          StrTmp.push_back(strTmp);
        } else{ 
          auto strTmp = mysprintf(MY_RESET ANSI_FONT_BOLD "Row:" ANSI_FONT_CLEAR "%u", iRowCount);
          StrTmp.push_back(strTmp);
          StrTmp.push_back("META MISMATCH (\"hashbert sync\" should've been run before running \"hashbert check\")");
          int tDiff=st.st_mtim.tv_sec-intTimeOld;
          const auto [tDiffHuman, charUnit]=getSuitableTimeUnit(tDiff);
          if(!boTMatch) {
            auto strTmp = mysprintf(ANSI_FONT_BOLD "tDiff:" ANSI_FONT_CLEAR "%i%c", tDiffHuman, charUnit);
            StrTmp.push_back(strTmp);
          }
          if(!boSizeMatch) {
            auto strTmp = mysprintf(ANSI_FONT_BOLD "size:" ANSI_FONT_CLEAR "%u/%u", intSizeOld, st.st_size);
            StrTmp.push_back(strTmp);
          }
          auto strTmpB = mysprintf("%s", strFile.c_str());
          StrTmp.push_back(strTmpB);
        }
        nMisMatchTimeSize++;
      }else{ // Meta data matches
        auto strTmp = mysprintf(MY_RESET ANSI_FONT_BOLD "Row:" ANSI_FONT_CLEAR "%u, Mismatch", iRowCount);
        StrTmp.push_back(strTmp);
        //StrTmp.push_back("(hash):"+std::string(strHashOld)+" / "+std::string(strHash));  
        StrTmp.push_back("(hash):"+strHashOld+" / "+strHash);   
        StrTmp.push_back(strFile);    
        nMisMatchHash++;
      }
      auto strTmp=join(StrTmp, ", ")+"\n";
      printf(strTmp.c_str());
      printf(MAKESPACE_N_SAVE);
    }
    else nOK++;  // Hashes match
  
  } // while(1)
  return "";
}


void helpTextExit( int argc, char **argv){
  printf("Help text: see https://emagnusandersson.com/hashbert\n", argv[0]);
  exit(0);
}



class InputParser{
  public:
    InputParser (int &argc, char **argv){
      for (int i=1; i < argc; ++i)
        this->tokens.push_back(std::string(argv[i]));
    }
    /// @author iain
    const std::string& getCmdOption(const std::string &option) const{
      std::vector<std::string>::const_iterator itr;
      itr =  std::find(this->tokens.begin(), this->tokens.end(), option);
      if (itr != this->tokens.end() && ++itr != this->tokens.end()){
        return *itr;
      }
      static const std::string empty_string("");
      return empty_string;
    }
    /// @author iain
    bool cmdOptionExists(const std::string &option) const{
      return std::find(this->tokens.begin(), this->tokens.end(), option)
        != this->tokens.end();
    }
  private:
    std::vector <std::string> tokens;
};

std::string trim(const std::string& str){
    int iStart;
    for(iStart = 0; str[iStart] == ' ' && iStart < str.size(); iStart++);  // iStart: 0..len-1
    int  iEnd;
    for(iEnd = str.size(); str[iEnd - 1] == ' ' && iEnd > iStart; iEnd--);  // iEnd: len..1
    int len=iEnd-iStart;
    
    return str.substr(iStart, len);
}


//#include <boost/algorithm/string.hpp>
int main( int argc, char **argv){
  char boCheck=0;
  //std::string strDir=".";
  //std::string strFileHashOld, strFileTmp, strFileHashNew;     strFileHashOld=strFileTmp=strFileHashNew="hashcodes.txt";
  //std::string strFileHashOld, strFileHashNew;     strFileHashOld=strFileHashNew="hashcodes.txt";
  
  InputParser input(argc, argv);
  if(argc==1 || input.cmdOptionExists("-h") || input.cmdOptionExists("--help") ){ helpTextExit(argc, argv);   }
  if(strcmp(argv[1],"sync")==0) boCheck=0;
  else if(strcmp(argv[1],"s")==0) boCheck=0;
  else if(strcmp(argv[1],"check")==0) boCheck=1;
  else if(strcmp(argv[1],"c")==0) boCheck=1;
  else { helpTextExit(argc, argv);   }
  
  //const std::string &strFilter = input.getCmdOption("-r");
  //if(!strFilter.empty()){
    //if(!boCheck) {printf("Filter rules (the -r option) can only be used with \"check\"\n"); exit(0);}
    //readFilterFrCommandLine(strFilter);
  //}
  //if(input.cmdOptionExists("-F")){ 
    //if(!boCheck) {printf("Filter rules (the -F option) can only be used with \"check\"\n"); exit(0);}
    //std::string strFileFilter = input.getCmdOption("-F");
    //if(strFileFilter.empty() || strFileFilter[0]=='-')  strFileFilter=".hashbert_filter";
    //if(readFilterFile(strFileFilter)) {perror("Error in readFilterFile"); return 1;}
  //}
  std::string strFileHashOld, strFileHashNew;  strFileHashOld=strFileHashNew= input.getCmdOption("-f");
  if(strFileHashOld.empty()) strFileHashOld=strFileHashNew="hashcodes.txt";
  const char* cstrFileHashOld=strFileHashOld.c_str();
   
  std::string strDir = input.getCmdOption("-d");  if(strDir.empty()) strDir=".";
  std::string strStart = input.getCmdOption("--start"); if(strStart.empty()) strStart="0";
  
  //const auto [strErr, fsDir]=myRealPath(strDir.c_str());
  const auto [strErr, fsDir]=myRealPath(strDir);
  if(strErr.size()) { perror("Error in myRealPath"); exit(EXIT_FAILURE); } 

  int intStart = std::stoi(strStart);
  


  //strFileHierarchy.append(".filesonly.tmp");
  strFileHashNew.append(".new.tmp");
  
  if(boCheck){
    std::string strErr=check(cstrFileHashOld, intStart); if(strErr!="") {std::cerr<<"Error in check: "<<strErr; return 1;}
  }else{
    int fd;
    int boDryRun = input.cmdOptionExists("-n"), boRealRun = !boDryRun;
    char strFileHierarchy[] = "/tmp/hashbert_tempfile";
    //std::fstream fptHierarchy(strFileHierarchy, std::ios::in|std::ios::out|std::ios::trunc);
    //if(fptHierarchy.fail()) { std::cerr<<strerror(errno)<<'\n'; }

    //FILE *fptHierarchy = fopen(strFileHierarchy, "w");
    //FILE *fptHierarchy=tmpfile();

    printf(R"(   t:       Elapsed time
   n:       File counter (current-file/total-number-of-files)
   touched: Size or mod-time has changed. (untouched: Size and mod-time are the same)
)"); // ANSI_CURSOR_SAVE  (so its hashcode is recalculated)
    printf("\n\n\n\n\n" ANSI_CURSOR_UP(5));  // Write some newlines (makes it scroll if you are on the bottom line), then go up the same amount of rows again 
    
    if(!boRealRun){ printf("(Dry-run)\n"); }
    printf("Parsing file tree\n" ANSI_CURSOR_SAVE);  // then save cursor
    //writeFolderContentToHierarchyFile(fptHierarchy, strDir);
    //writeFolderContentToHierarchyFile(fptHierarchy, fsDir, "");
    std::vector<RowTreeT> arrTree(0);
    parseTree(arrTree, fsDir, "");
    //FILE *fptOld = fopen(cstrFileHashOld, "a+");  // By using "a+" (reading and appending) the file is created if it doesn't exist.
    //FILE *fptNew = fopen(strFileHashNew.c_str(), "w");
    std::ifstream fptOld(cstrFileHashOld);
    std::ofstream fptNew(strFileHashNew);
    //rewind(fptHierarchy);
    //fptHierarchy.clear();  fptHierarchy.seekg(0);
    printf(ANSI_CURSOR_RESTORE ANSI_CURSOR_UP(1) ANSI_CLEAR_BELOW "Syncing hashcode file\n" ANSI_CURSOR_SAVE);

    //if(mergeOldNHierarchy(fptNew, fptOld, fptHierarchy, fsDir, boRealRun)) {perror("Error in mergeOldNHierarchy"); return 1;}
    if(mergeOldNHierarchy(fptNew, fptOld, arrTree, fsDir, boRealRun)) {perror("Error in mergeOldNHierarchy"); return 1;}
    //fclose(fptHierarchy); fclose(fptOld); fclose(fptNew);
    
    if(boRealRun){
      if(rename(strFileHashNew.c_str(), cstrFileHashOld)) perror("");
    } 
  }
  return 0;
}



