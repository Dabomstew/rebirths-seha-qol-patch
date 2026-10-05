#include "prepare_assets_native.hpp"
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <string>

int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=5)throw std::runtime_error("usage: prepare-assets-test prepare|verify|prepare-cancel|prepare-ma123|verify-ma123|prepare-large-ma|verify-large-ma|prepare-adv-cg|verify-adv-cg game output game-id");
        unsigned prepared=0;
        auto report=[&](const rebirths::prepare::Progress& p){if(p.stage==L"Preparing raw PAC")prepared++;
            std::fwprintf(stderr,L"%ls %ls %llu/%llu\n",p.stage.c_str(),p.current.c_str(),p.completed,p.total);};
        auto action=std::wstring(argv[1]);auto cancel=[&]{return action==L"prepare-cancel"&&prepared>=2;};
        using rebirths::prepare::TransformProfile;
        auto profile=TransformProfile::Raw;
        if(action==L"prepare-ma123"||action==L"verify-ma123")profile=TransformProfile::Rb3Ma123Pilot;
        if(action==L"prepare-large-ma"||action==L"verify-large-ma")profile=TransformProfile::Rb3LargeMaPilot;
        if(action==L"prepare-adv-cg"||action==L"verify-adv-cg")profile=TransformProfile::AdvCgHalf24V1;
        auto verify=action==L"verify"||action==L"verify-ma123"||action==L"verify-large-ma"||action==L"verify-adv-cg";
        auto result=!verify?
            rebirths::prepare::RunAssets(argv[2],argv[3],std::stoul(argv[4]),report,cancel,profile):
            rebirths::prepare::VerifyAssets(argv[2],argv[3],std::stoul(argv[4]),report,{},profile);
        std::printf("files=%llu bytes=%llu reused=%llu full_coverage=%d\n",result.files,result.bytes,result.reused,result.fullCoverage);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
