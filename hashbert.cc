#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h> // check if directory exist
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strncpy
#include <libgen.h> // basename

#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>    // std::sort, std::find
#include <string>       // std::to_string
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <errno.h>
//#include <format>
//#include <filesystem> // fs::is_directory

#if defined(__APPLE__)
#  define COMMON_DIGEST_FOR_OPENSSL
#  include <CommonCrypto/CommonDigest.h>
#  define SHA1 CC_SHA1
#else
#  include <openssl/md5.h>
#endif

#define LENFILENAME 1024

//namespace fs = std::filesystem;

//
// Compile with:
//   g++ -o hashbert hashbert.cc -lcrypto -std=gnu++20 -O3
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

#define MAKESPACE_N_SAVE "\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE
//#define MAKESPACE_N_SAVE "\n\n\033[2A\0337";
//const char* MAKESPACE_N_SAVE={"\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE};



#define L_BUFFER 1024
void calcHashOfFile(std::ifstream& fin, char out[33]) {
  int i, nRead;
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
    snprintf(&(out[i*2]), 3, "%02x", (unsigned int)digest[i]);  // Each "write" will write 3 bytes (2 + 1 null)
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
  
char strStatus[]="(" ANSI_FONT_BOLD "t" ANSI_FONT_CLEAR ": %u:%02u:%02u, " 
ANSI_FONT_BOLD "n" ANSI_FONT_CLEAR ": %u/%u, " 
ANSI_FONT_BOLD "touched" ANSI_FONT_CLEAR ": %u, " 
ANSI_FONT_BOLD "untouched" ANSI_FONT_CLEAR ": %u, " 
ANSI_FONT_BOLD "new" ANSI_FONT_CLEAR ": %u, " 
ANSI_FONT_BOLD "deleted" ANSI_FONT_CLEAR ": %u)\n";

void writeFolderContentToHierarchyFile(FILE *fpt, const std::string strDir){  // Write folder content (that is files only) of strDir. For folders in strDir recursive calls are made.
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
     
  if (!(dir = opendir(strDir.c_str())))        return;
  
  while(1) {
    if (!(entry = readdir(dir))) break;
    vecEntry.push_back(EntryT(entry->d_name, entry->d_type == DT_DIR));
  }
  std::sort(std::begin(vecEntry), std::end(vecEntry));

  for(auto it=vecEntry.begin(); it!=vecEntry.end(); ++it) {
  //for(std::vector<EntryT>::iterator it=vecEntry.begin(); it!=vecEntry.end(); ++it) {
  //for(std::vector<EntryT>::size_type i = 0; i != vecEntry.size(); i++) {
    std::string strPath;
    EntryT ent=*it;

    //strPath=strDir; 
    if(strDir==".") strPath=ent.str;
    else {strPath=strDir; strPath.append("/").append(ent.str); }
    if (ent.boDir) {
      if (strcmp(ent.str.c_str(), ".") == 0 || strcmp(ent.str.c_str(), "..") == 0)     continue;
      writeFolderContentToHierarchyFile(fpt, strPath);
    }
    else {
      struct stat st;
      if(stat(strPath.c_str(), &st) != 0) perror(strPath.c_str());
      fprintf(fpt, "%u %u %s\n", st.st_mtim.tv_sec, st.st_size, strPath.c_str()); nFile++;
      //if(1){
      if(nFile%100==0){
        //printf(); // Reset cursor and clear everything below
        printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW);
        int nHour, nMin, nSec;    toc(nHour, nMin, nSec);
        //printf("(t: %u:%02u:%02u, n: %u)\n", nHour, nMin, nSec, nFile);
        printf(strStatus, nHour, nMin, nSec, 0, nFile, 0, 0, 0, 0);
        //printf("Parsing file tree\n");
      }
    }
  } 
  closedir(dir);
}

int mergeOldNHierarchy(FILE *fptNew, FILE *fptOld, FILE *fptHierarchy, int boRealRun){  // Merge fptOld and fptHierarchy to fptNew and recalculate hashcode for new files and when file-mod-time/size is different
  char strHashOld[33], strHashCalc[33], *strHash;
  //char boOldGotStuff=1, boHierarchyGotStuff=1;   // Should be inverted and called boOldEndReached, boHierarchyEndReached
  char boOldEndReached=0, boHierarchyEndReached=0;
  char boMakeReadOld=1, boMakeReadHierarchy=1;
  unsigned int nRow=0, nUntouched=0, nTouched=0, nDelete=0, nNew=0;
  unsigned int intTimeOld, intSizeOld, intTimeHierarchy, intSizeHierarchy;
  char strFileOld[LENFILENAME], strFileHierarchy[LENFILENAME];

  //FILE *fptLog = fopen("log.txt", "w");
    
  enum Status { DONE, DELETED, UNTOUCHED, TOUCHED, NEWFILE };
  enum Status myStatus;
  while(1) {
    int intCmp;
    if(boMakeReadOld) { if(fscanf(fptOld, "%32s %u %u %1023[^\n]\n", strHashOld, &intTimeOld, &intSizeOld, strFileOld)!=4) boOldEndReached=1;     boMakeReadOld=0;  }
    if(boMakeReadHierarchy) { if(fscanf(fptHierarchy, "%u %u %1023[^\n]\n", &intTimeHierarchy, &intSizeHierarchy, strFileHierarchy)!=3) boHierarchyEndReached=1;     boMakeReadHierarchy=0;  }
    
    
    if(!boOldEndReached && !boHierarchyEndReached){
      intCmp=strcmp(strFileOld, strFileHierarchy);
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
    } else {  printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW); }
        
    int nHour, nMin, nSec;    toc(nHour, nMin, nSec);
    //printf("(%u:%02u:%02u", nHour, nMin, nSec);
    //printf("File " ANSI_FONT_BOLD "%u/%u" ANSI_FONT_CLEAR ": " , nRow, nFile); 
    
    printf(strStatus, nHour, nMin, nSec, nRow, nFile, nTouched, nUntouched, nNew, nDelete);
    
    if(myStatus==DONE){  break; }
    else if(myStatus==DELETED){ nDelete++; }  //printf("File removed.\n");
    else if(myStatus==UNTOUCHED){ nUntouched++; strHash=strHashOld; }  // printf("modTime-size match (Reusing hash).\n");  printf("\n");
    else if(myStatus==TOUCHED){ printf("Touched (Recalculating hash):\n"); nTouched++; strHash=strHashCalc; }
    else if(myStatus==NEWFILE){ printf("New file (Calculating hash):\n");  nNew++; strHash=strHashCalc; }
    
      // Write file name to screen. 
      // Only do this when the hash needs to be calculated (For prestanda reasons. (The file names will be flashing by to fast to read anyhow.))
    if(myStatus>=TOUCHED ){ printf("%s\n", strFileHierarchy);  }

      // Calculate hash
    if(myStatus>=TOUCHED ){
      if(boRealRun){

        std::ifstream fpt(strFileHierarchy, std::ifstream::binary);
        if(fpt.fail()) {  perror(strFileHierarchy);   return 1;  }
        calcHashOfFile(fpt, strHash);

        //fprintf(fptLog, "%s %10u %10u %s\n", "strHash", intTimeHierarchy, intSizeHierarchy, strFileHierarchy); 
      }
    }
      // Write to temporary (new) HASHFILE
    if(myStatus>=UNTOUCHED){   fprintf(fptNew, "%s %10u %10u %s\n", strHash, intTimeHierarchy, intSizeHierarchy, strFileHierarchy); }
  }
  
  return 0;
}

// void join(const std::vector<std::string> &v,  std::string c,  std::string &s){
//   s.clear();
//   for(auto p=v.begin();  p!=v.end();  ++p) {
//   //for(std::vector<std::string>::const_iterator p=v.begin();  p!=v.end();  ++p) {
//     s+=*p;
//     if(p!=v.end()-1) s+=c;
//   }
// }
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
    if( stat( strPathC, &info ) == 0 ) { return strPathCL; }
    free(strPathCL);
    strPathCL=strdup(strPathC);
  }
}
// void getSuitableTimeUnit(int t, int &v, char &charUnit){ // t in seconds
//   int tAbs=abs(t), tSign=t>=0?+1:-1;
//   if(tAbs<=120) {v=tSign*tAbs; charUnit='s'; return;}
//   tAbs/=60; // t in minutes
//   if(tAbs<=120) {v=tSign*tAbs; charUnit='m'; return;} 
//   tAbs/=60; // t in hours
//   if(tAbs<=48) {v=tSign*tAbs; charUnit='h'; return;}
//   tAbs/=24; // t in days
//   if(tAbs<=2*365) {v=tSign*tAbs; charUnit='d'; return;}
//   tAbs/=365; // t in years
//   v=tSign*tAbs; charUnit='y'; return;
// }
struct struct_out_getSuitableTimeUnit{
  int v;
  char charUnit;
};
struct_out_getSuitableTimeUnit getSuitableTimeUnit(const int t){ // t in seconds
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

template<typename ... Args>
char* mysprintf(const char* fmt, Args ... args){
  const int L_STRTMP=300;
  static char strTmp[L_STRTMP];
  auto intL=snprintf(strTmp, L_STRTMP, fmt, args ...);
  return strTmp;
}

//void check(FILE *fpt){  // Go through the hashcode-file, for each file, check if the hashcode matches the actual files hashcode
//int check(std::string strFileHashOld, int intStart){  // Go through the hashcode-file, for each row (file), check if the hashcode matches the actual files 
int check(const char* cstrFileHashOld, int intStart){  // Go through the hashcode-file, for each row (file), check if the hashcode matches the actual files hashcode  
  char strHash[33];
  unsigned int iRowCount=0, nNotFound=0, nMisMatchTimeSize=0, nMisMatchHash=0, nOK=0;
  //printf("OK\n");
  
  if(access(cstrFileHashOld, F_OK)==-1) {perror(""); return 1;}
  FILE *fptHashOld = fopen(cstrFileHashOld, "r");

    
  printf(MAKESPACE_N_SAVE);
  
  std::string strMissingFile, strHighestFolderMissing;
  unsigned int nNotFoundLoc, iNotFoundFirst;
  int lenStrHighestFolderMissing;
  char charMissing=' ';
  
  while(1) {
    int intTimeOld, intSizeOld;
    char boGotRow=1;
    char strHashOld[33], strFile[LENFILENAME];
    if(fscanf(fptHashOld,"%32s %u %u %1023[^\n]",strHashOld, &intTimeOld, &intSizeOld, strFile)!=4) boGotRow=0;
    int nHour, nMin, nSec;
    if(boGotRow){
      iRowCount++;
      if(iRowCount<intStart) { continue;}  // iRowCount / intStart (row number) is 1-indexed

      if(charMissing=='d'){
        if(strHighestFolderMissing.compare(0, std::string::npos, strFile, lenStrHighestFolderMissing)!=0){ // If directory was missing, and now strFile refers to something outside that directory.
          if(nNotFoundLoc==1) printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, Missing file: %s\n", iNotFoundFirst, strMissingFile.c_str());
          else printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u-%u (%u), Missing folder: %s\n", iNotFoundFirst, iNotFoundFirst+nNotFoundLoc-1, nNotFoundLoc, strHighestFolderMissing.c_str());
          printf(MAKESPACE_N_SAVE);
          strHighestFolderMissing=""; lenStrHighestFolderMissing=0; charMissing=' '; nNotFoundLoc=0;
        }else{ nNotFoundLoc++; nNotFound++; continue; }
      }
      
      toc(nHour, nMin, nSec);
      printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "%u:%02u:%02u, Checking row: %u (%s)\n", nHour, nMin, nSec, iRowCount, strFile);


      std::ifstream fpt(strFile, std::ifstream::binary);
      int boFileFound=!fpt.fail();

      if(!boFileFound) {
        int errnoTmp = errno;
        if(errnoTmp==ENOENT) {
          std::string strTmp=getHighestMissing(strFile);
          char charMissingCur=strTmp.compare(0, std::string::npos, strFile)==0?'f':'d'; // f=file missing, d=directory missing
          if(charMissingCur=='f') {
            if(charMissingCur!=charMissing) { // If first occurance
              charMissing='f';  strMissingFile=strFile;
              //strLabel=dirname(strdup(strMissingFile.c_str()));
              iNotFoundFirst=iRowCount;  nNotFoundLoc=0;
            }
            nNotFound++; nNotFoundLoc++;
          }else{  // If charMissingCur=='d'
            if(charMissing=='f'){ // If charMissing is switched (f to d).
              if(nNotFoundLoc==1) printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, Missing file: %s\n", iNotFoundFirst, strMissingFile.c_str());
              else printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u-%u, %u missing files in: %s\n", iNotFoundFirst, iNotFoundFirst+nNotFoundLoc-1, nNotFoundLoc, dirname(strdup(strMissingFile.c_str())));
              printf(MAKESPACE_N_SAVE);
            }
            if(charMissingCur!=charMissing) { // If first occurance
              charMissing='d';  strMissingFile=strFile;
              //strLabel=dirname(strdup(strMissingFile.c_str()));
              iNotFoundFirst=iRowCount;  nNotFoundLoc=0;
              strHighestFolderMissing=strTmp;
              lenStrHighestFolderMissing=strHighestFolderMissing.length();
            }
            nNotFound++; nNotFoundLoc++;
          }
          //printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, ENOENT (file not found): %s\n", iRowCount, strFile);
          //printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, Missing: %s\n", iRowCount, strHighestFolderMissing.c_str());
          //printf(MAKESPACE_N_SAVE);
        }
        else {perror(strFile); return 1; }
      }  // boFileFound==false
      else{  // boFileFound==true
        
        if(charMissing=='f'){ // If file was missing, and now things are found.
          if(nNotFoundLoc==1) printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, Missing file: %s\n", iNotFoundFirst, strMissingFile.c_str());
          else printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u-%u, %u missing files in: %s\n", iNotFoundFirst, iNotFoundFirst+nNotFoundLoc-1, nNotFoundLoc, dirname(strdup(strMissingFile.c_str())));
          printf(MAKESPACE_N_SAVE);
          charMissing=' '; nNotFoundLoc=0;
        }
        calcHashOfFile(fpt, strHash);
        
        if(strncmp(strHashOld, strHash, 32)!=0){
          
            // Check modTime and size (perhaps the user forgott to run sync before running check
          struct stat st;
          if(stat(strFile, &st) != 0) perror(strFile);
          int boTMatch=st.st_mtim.tv_sec==intTimeOld, boSizeMatch=st.st_size==intSizeOld;
          std::vector <std::string> StrTmp;
          if(!boTMatch || !boSizeMatch ){
            if(strcmp(basename(strFile), cstrFileHashOld)==0) { 
              auto strTmp=mysprintf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, ( %s is ignored)", iRowCount, strFile);
              StrTmp.push_back(strTmp);
            } else{ 
              auto strTmp = mysprintf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u", iRowCount);
              StrTmp.push_back(strTmp);
              StrTmp.push_back("META MISMATCH (run hashbert sync)");
              int tDiff=st.st_mtim.tv_sec-intTimeOld;
              const auto [tDiffHuman, charUnit]=getSuitableTimeUnit(tDiff);
              if(!boTMatch) {
                auto strTmp = mysprintf("tDiff:%i%c", tDiffHuman, charUnit);
                StrTmp.push_back(strTmp);
              }
              if(!boSizeMatch) {
                auto strTmp = mysprintf("size:%u/%u", intSizeOld, st.st_size);
                StrTmp.push_back(strTmp);
              }
              auto strTmpB = mysprintf("%s", strFile);
              StrTmp.push_back(strTmpB);
            }
            nMisMatchTimeSize++;
          }else{
            auto strTmp = mysprintf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%u, Mismatch", iRowCount);
            StrTmp.push_back(strTmp);
            StrTmp.push_back("(hash):"+std::string(strHashOld)+" / "+std::string(strHash));   
            StrTmp.push_back(strFile);    
            nMisMatchHash++;
          }
          auto strTmp=join(StrTmp, ", ")+"\n";
          printf(strTmp.c_str());
          printf(MAKESPACE_N_SAVE);
        }
        else nOK++;
      }  // boGotRow==true
    }else { // boGotRow==false
      toc(nHour, nMin, nSec);
      printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Time: %u:%02u:%02u, Done (RowCount: %u, NotFound: %u, MisMatchTimeSize: %u, MisMatchHash: %u, OK: %u)\n", nHour, nMin, nSec, iRowCount, nNotFound, nMisMatchTimeSize, nMisMatchHash, nOK);
      break;
    }
  } // while(1)
  return 0;
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


//#include <boost/algorithm/string.hpp>
int main( int argc, char **argv){
  char boCheck=0;
  //std::string strDir=".";
  //std::string strFileHashOld, strFileTmp, strFileHashNew;     strFileHashOld=strFileTmp=strFileHashNew="hashcodes.txt";
  //std::string strFileHashOld, strFileHashNew;     strFileHashOld=strFileHashNew="hashcodes.txt";
  
  InputParser input(argc, argv);
  if(argc==1 || input.cmdOptionExists("-h") || input.cmdOptionExists("--help") ){ helpTextExit(argc, argv);   }
  if(strcmp(argv[1],"sync")==0) boCheck=0;
  else if(strcmp(argv[1],"check")==0) boCheck=1;
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
  
  int intStart = std::stoi(strStart);
  


  //strFileHierarchy.append(".filesonly.tmp");
  strFileHashNew.append(".new.tmp");
  
  if(boCheck){
    if(check(cstrFileHashOld, intStart)) {perror("Error in check"); return 1;}
  }else{
    int fd;
    int boDryRun = input.cmdOptionExists("-n"), boRealRun = !boDryRun;
    //char strFileHierarchy[] = "/tmp/fileXXXXXX";
    //FILE *fptHierarchy = fopen(strFileHierarchy, "w");
    FILE *fptHierarchy=tmpfile();

    printf(R"(   t:       Elapsed time
   n:       File counter (current-file/total-number-of-files)
   touched: Size or mod-time has changed. (untouched: Size and mod-time are the same)
)"); // ANSI_CURSOR_SAVE  (so its hashcode is recalculated)
    printf("\n\n\n\n\n" ANSI_CURSOR_UP(5));  // Write some newlines (makes it scroll if you are on the bottom line), then go up the same amount of rows again 
    //printf("Searching files...\n" ANSI_CURSOR_SAVE);  // Save cursor at the end
    if(!boRealRun){ printf("(Dry-run)\n"); }
    printf("Parsing file tree\n" ANSI_CURSOR_SAVE);  // then save cursor
    //printf(ANSI_CURSOR_SAVE);  // then save cursor
    writeFolderContentToHierarchyFile(fptHierarchy, strDir);
    FILE *fptOld = fopen(cstrFileHashOld, "a+");  // By using "a+" (reading and appending) the file is created if it doesn't exist.
    FILE *fptNew = fopen(strFileHashNew.c_str(), "w");
    rewind(fptHierarchy);
    printf(ANSI_CURSOR_RESTORE ANSI_CURSOR_UP(1) ANSI_CLEAR_BELOW "Syncing hashcode file\n" ANSI_CURSOR_SAVE);

    if(mergeOldNHierarchy(fptNew, fptOld, fptHierarchy, boRealRun)) {perror("Error in mergeOldNHierarchy"); return 1;}
    fclose(fptHierarchy); fclose(fptOld); fclose(fptNew);
    
    if(boRealRun){
      if(rename(strFileHashNew.c_str(), cstrFileHashOld)) perror("");
    } 
  }
  return 0;
}



