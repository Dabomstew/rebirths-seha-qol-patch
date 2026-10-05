#include "asset_store.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=4)return 2;
        auto store=rebirths::assets::Load(argv[1],argv[2],std::stoul(argv[3]));
        size_t count=0;
        for(const auto& group:store.groups)for(const auto& row:group.second){
            auto a=rebirths::assets::OpenAsset(store,row.second,true);
            auto b=rebirths::assets::OpenAsset(store,row.second,true);
            LARGE_INTEGER at{};at.QuadPart=row.second.offset;
            if(!SetFilePointerEx(a.value,at,nullptr,FILE_BEGIN))return 3;
            LARGE_INTEGER zero{},other{};
            if(!SetFilePointerEx(b.value,zero,&other,FILE_CURRENT)||other.QuadPart)return 4;
            ++count;
        }
        std::cout<<count<<" "<<store.rejectedGroups<<" "<<store.backend<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
