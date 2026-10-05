#include "asset_store.hpp"
#include "adv_cg_specs.hpp"
#include <algorithm>
#include <cstring>
#include <climits>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace rebirths::assets {
namespace fs=std::filesystem;
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using rebirths::platform::Hex;
using rebirths::platform::Sha;
std::wstring Extended(const fs::path& p){
    auto s=fs::absolute(p).wstring();if(s.rfind(L"\\\\?\\",0)==0)return s;
    if(s.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+s.substr(2);return L"\\\\?\\"+s;
}
void ReadAt(HANDLE file,uint64_t offset,void* output,DWORD count){
    LARGE_INTEGER at{};at.QuadPart=static_cast<LONGLONG>(offset);DWORD got=0;
    Need(offset<=uint64_t(INT64_MAX)&&SetFilePointerEx(file,at,nullptr,FILE_BEGIN)&&
         ReadFile(file,output,count,&got,nullptr)&&got==count,"Truncated file read");
}
struct Reader {
    std::vector<unsigned char> bytes;size_t pos=0;
    void Take(void* p,size_t n){Need(pos<=bytes.size()&&n<=bytes.size()-pos,"Truncated manifest");std::memcpy(p,bytes.data()+pos,n);pos+=n;}
    template<class T>T Number(){T v{};Take(&v,sizeof(v));return v;}
    std::string String(){auto n=Number<uint32_t>();Need(n&&n<4096,"Manifest string bound");std::string s(n,'\0');Take(s.data(),n);Need(CanonicalPath(s)==s,"Noncanonical manifest path");return s;}
};
struct Source {std::string path,group;uint32_t kind;uint64_t size,ticks,indexSize;Hash hash,indexHash;};
struct Manifest {uint32_t game,backend;std::vector<Source> sources;std::vector<std::pair<uint32_t,File>> files;};
Manifest Parse(const fs::path& path){
    auto handle=OpenRead(path);auto size=FileSize(handle.value);Need(size>=56&&size<=128*1024*1024+32,"Manifest size bound");
    Reader r;r.bytes.resize(static_cast<size_t>(size));ReadAt(handle.value,0,r.bytes.data(),static_cast<DWORD>(size));
    Sha sha;sha.Add(r.bytes.data(),r.bytes.size()-32);auto digest=sha.Finish();
    Need(!std::memcmp(digest.data(),r.bytes.data()+r.bytes.size()-32,32),"Manifest digest mismatch");r.bytes.resize(r.bytes.size()-32);
    char magic[8];r.Take(magic,8);Need(!std::memcmp(magic,"RBAST001",8),"Manifest format");
    Manifest m{};m.game=r.Number<uint32_t>();m.backend=r.Number<uint32_t>();
    auto ns=r.Number<uint32_t>(),nf=r.Number<uint32_t>();Need(m.game<4&&m.backend<2&&ns<=100000&&nf<=1000000,"Manifest header bounds");
    std::unordered_set<std::string> sourcePaths;
    for(uint32_t i=0;i<ns;i++){
        Source s{};s.path=r.String();s.group=r.String();s.kind=r.Number<uint32_t>();s.size=r.Number<uint64_t>();s.ticks=r.Number<uint64_t>();s.indexSize=r.Number<uint64_t>();
        r.Take(s.hash.data(),32);r.Take(s.indexHash.data(),32);
        Need(s.kind<2&&s.indexSize<=s.size&&s.indexSize<=64*1024*1024,"Manifest source bounds");
        Need(sourcePaths.insert(CanonicalPath(s.path,true)).second,"Duplicate manifest source");m.sources.push_back(std::move(s));
    }
    std::set<std::pair<std::string,uint32_t>> keys;
    for(uint32_t i=0;i<nf;i++){
        auto source=r.Number<uint32_t>();File f;f.id=r.Number<uint32_t>();f.path=r.String();f.offset=r.Number<uint64_t>();f.storageSize=r.Number<uint64_t>();f.size=r.Number<uint32_t>();
        r.Take(f.hash.data(),32);r.Take(f.metadata.data(),288);
        Need(source<ns&&m.sources[source].kind==1,"Manifest asset source");
        Need(f.storageSize<0x80000000ULL&&f.offset<=f.storageSize&&f.size<=f.storageSize-f.offset,"Manifest asset range");
        Need(m.backend!=0||(f.offset==0&&f.storageSize==f.size),"Loose range mismatch");
        Need(m.backend!=1||f.storageSize<=0x60000000ULL,"PAC exceeds native bound");
        uint32_t expectedSize;std::memcpy(&expectedSize,f.metadata.data()+276,4);
        Need(std::memchr(f.metadata.data()+8,0,260),"Manifest asset metadata");
        bool knownHalf=false;
        if(expectedSize!=f.size)for(const auto& spec:rebirths::advcg::kSpecs){
            if(spec.game==m.game&&spec.id==f.id&&spec.sourceSize==expectedSize&&
               spec.outputSize==f.size&&
               CanonicalPath(m.sources[source].path,true)==spec.archive&&
               std::strcmp(reinterpret_cast<const char*>(f.metadata.data()+8),spec.name)==0&&
               Hex(f.hash)==spec.outputHash){knownHalf=true;break;}
        }
        Need(expectedSize==f.size||knownHalf,"Manifest asset size differs from source");
        Need(keys.emplace(m.sources[source].group,f.id).second,"Duplicate asset identity");m.files.emplace_back(source,std::move(f));
    }
    Need(r.pos==r.bytes.size(),"Manifest trailing data");return m;
}
std::string GroupFor(const std::string& path){
    auto s=CanonicalPath(path,true);
    if(s.size()>4&&s.substr(s.size()-4)==".cpk")return s.substr(0,s.size()-4);
    Need(s.size()>9&&s.substr(s.size()-4)==".pac","Unsupported source suffix");
    for(size_t i=s.size()-9;i<s.size()-4;i++)Need(s[i]>='0'&&s[i]<='9',"PAC part suffix");
    return s.substr(0,s.size()-9);
}
std::unordered_map<std::string,std::string> Discover(const fs::path& game,uint32_t id,bool dlc){
    std::vector<std::string> names=dlc?(id==3?std::vector<std::string>{"DLC_EN","DLC_JP","DLC_CN"}:std::vector<std::string>{"DLC"}):std::vector<std::string>{"data"};
    std::unordered_map<std::string,std::string> result;
    for(const auto& name:names){
        const auto root=CheckedPath(game,name);if(!fs::exists(root))continue;Need(fs::is_directory(root),"Source root is not a directory");
        for(const auto& file:fs::recursive_directory_iterator(root)){
            CheckedPath(game,file.path().lexically_relative(game).generic_string());if(!file.is_regular_file())continue;
            auto ext=file.path().extension().string();
            for(auto& c:ext)if(c>='A'&&c<='Z')c=char(c+32);
            if(ext!=".pac"&&ext!=".cpk")continue;
            auto relative=CanonicalPath(file.path().lexically_relative(game).generic_string(),true);
            Need(result.emplace(relative,GroupFor(relative)).second,"Case-colliding source paths");
        }
    }
    return result;
}
void AddScope(Store& result,const fs::path& game,uint32_t id,bool dlc){
    auto installed=Discover(game,id,dlc);if(dlc&&installed.empty())return;
    const auto manifestPath=CheckedPath(result.root,dlc?"dlc.manifest":"base.manifest");
    if(dlc&&!fs::exists(manifestPath))return;
    auto m=Parse(manifestPath);Need(m.game==id,"Wrong manifest game");
    if(result.count)Need(m.backend==result.backend,"Mixed backend scopes");result.backend=m.backend;
    std::unordered_set<std::string> rejected,recorded,cpk;
    std::vector<Handle> locks;
    std::unordered_map<std::string,std::vector<unsigned char>> tables;
    for(const auto& s:m.sources){
        Need(GroupFor(s.path)==s.group,"Source namespace mismatch");auto path=CanonicalPath(s.path,true);recorded.insert(path);
        Need((path.substr(path.size()-4)==".cpk")== (s.kind==0),"Source kind mismatch");
        auto found=installed.find(path);if(found==installed.end()){rejected.insert(s.group);continue;}
        try{
            auto h=OpenRead(CheckedPath(game,s.path));BY_HANDLE_FILE_INFORMATION info{};
            Need(GetFileInformationByHandle(h.value,&info)!=0,"Source information failed");
            uint64_t time=uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32|info.ftLastWriteTime.dwLowDateTime;
            Need(FileSize(h.value)==s.size&&time==s.ticks&&HashRange(h.value,0,s.indexSize)==s.indexHash,"Source identity mismatch");
            if(s.kind==0)Need(s.indexSize==s.size,"Incomplete CPK identity");
            else{
                Need(s.indexSize>=20,"PAC table bound");
                auto& table=tables[s.path];table.resize(static_cast<size_t>(s.indexSize));
                ReadAt(h.value,0,table.data(),static_cast<DWORD>(table.size()));
                uint32_t unknown,count,part;
                std::memcpy(&unknown,table.data()+8,4);std::memcpy(&count,table.data()+12,4);std::memcpy(&part,table.data()+16,4);
                Need(!std::memcmp(table.data(),"DW_PACK\0",8)&&unknown==0&&count&&count<=65536&&
                     s.indexSize==20+uint64_t(count)*288&&part==std::stoul(s.path.substr(s.path.size()-9,5)),"PAC index mismatch");
            }
            if(s.kind==0)cpk.insert(s.group);locks.push_back(std::move(h));
        }catch(...){rejected.insert(s.group);}
    }
    for(const auto& item:installed)if(!recorded.count(item.first))rejected.insert(item.second);
    for(const auto& s:m.sources)if(!cpk.count(s.group))rejected.insert(s.group);
    // Validate entire scope before publishing any mappings or ownership into result.
    for(auto& pair:m.files){auto& f=pair.second;const auto& source=m.sources[pair.first];
        Need(f.path.rfind(dlc?"dlc/":"base/",0)==0,"Generated scope mismatch");
        const auto sourcePart=std::stoul(source.path.substr(source.path.size()-9,5));Need((f.id>>16)==sourcePart,"Original part identity mismatch");
        if(!rejected.count(source.group)){
            const auto& table=tables.at(source.path);auto offset=20+size_t(f.id&65535)*288;
            Need(offset<=table.size()&&288<=table.size()-offset&&
                 !std::memcmp(f.metadata.data(),table.data()+offset,288),"Original entry metadata mismatch");
        }
        f.dlc=dlc;
    }
    result.rejectedGroups+=static_cast<uint32_t>(rejected.size());
    for(auto& pair:m.files){const auto& group=m.sources[pair.first].group;
        if(!rejected.count(group)){result.groups[group].emplace(pair.second.id,std::move(pair.second));result.count++;}}
    for(auto& h:locks)result.sourceLocks.push_back(std::move(h));
}
}

std::string CanonicalPath(std::string s,bool manager){
    if(manager){std::replace(s.begin(),s.end(),'\\','/');while(s.rfind("./",0)==0)s.erase(0,2);for(auto& c:s)if(c>='A'&&c<='Z')c=char(c+32);}
    Need(!s.empty()&&s.size()<4096&&s.front()!='/'&&s.back()!='/',"Invalid relative path");
    size_t start=0;
    while(start<s.size()){
        auto end=s.find('/',start);if(end==std::string::npos)end=s.size();auto part=s.substr(start,end-start);
        Need(!part.empty()&&part!="."&&part!=".."&&part.back()!='.'&&part.back()!=' ',"Unsafe relative component");
        for(unsigned char c:part)Need(c>=32&&c<127&&!std::strchr("\\:*?\"<>|",c),"Unsupported path character");
        auto stem=part.substr(0,part.find('.'));for(auto& c:stem)if(c>='A'&&c<='Z')c=char(c+32);
        Need(stem!="con"&&stem!="prn"&&stem!="aux"&&stem!="nul"&&!(stem.size()==4&&(stem.substr(0,3)=="com"||stem.substr(0,3)=="lpt")&&stem[3]>='1'&&stem[3]<='9'),"Reserved path");
        start=end+1;
    }
    return s;
}
fs::path CheckedPath(const fs::path& root,const std::string& relative){
    const auto boundary=fs::absolute(root).lexically_normal();auto result=boundary;
    if(!relative.empty())result/=fs::path(CanonicalPath(relative));
    for(auto p=result;!p.empty();){
        auto attributes=GetFileAttributesW(Extended(p).c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Reparse point refused: "+p.u8string());
        if(p==boundary||p.lexically_relative(boundary)==L".")break;
        auto parent=p.parent_path();if(parent==p)break;p=parent;
    }
    return result;
}
Handle OpenRead(const fs::path& path){
    Handle h(CreateFileW(Extended(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    Need(h.value!=INVALID_HANDLE_VALUE,"Cannot open asset/source");BY_HANDLE_FILE_INFORMATION info{};
    Need(GetFileInformationByHandle(h.value,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"Unsafe asset/source handle");return h;
}
uint64_t FileSize(HANDLE file){LARGE_INTEGER s{};Need(GetFileSizeEx(file,&s)&&s.QuadPart>=0,"Cannot get file size");return static_cast<uint64_t>(s.QuadPart);}
Hash HashRange(HANDLE file,uint64_t start,uint64_t length){
    auto size=FileSize(file);Need(start<=size&&length<=size-start,"Hash range outside file");Sha sha;
    std::vector<unsigned char> buffer(1024*1024);
    for(uint64_t at=0;at<length;){auto count=static_cast<DWORD>(std::min<uint64_t>(buffer.size(),length-at));ReadAt(file,start+at,buffer.data(),count);sha.Add(buffer.data(),count);at+=count;}
    return sha.Finish();
}
Store Load(const fs::path& root,const fs::path& gameRoot,uint32_t game){
    Need(game<4,"Unsupported game");Store result;result.root=CheckedPath(root);
    AddScope(result,CheckedPath(gameRoot),game,false);
    try{AddScope(result,CheckedPath(gameRoot),game,true);}catch(const std::exception& e){result.diagnostics.push_back(std::string("DLC unavailable: ")+e.what());}
    Need(result.count!=0,"No prepared assets match sources");return result;
}
Handle OpenAsset(const Store& store,const File& f,bool verify){
    auto h=OpenRead(CheckedPath(store.root,f.path));Need(FileSize(h.value)==f.storageSize,"Backing size mismatch");
    Need(f.offset<=f.storageSize&&f.size<=f.storageSize-f.offset,"Asset range mismatch");
    if(verify)Need(HashRange(h.value,f.offset,f.size)==f.hash,"Asset hash mismatch");
    LARGE_INTEGER zero{};Need(SetFilePointerEx(h.value,zero,nullptr,FILE_BEGIN)!=0,"Asset rewind failed");return h;
}
}
