#include "prepare_game.hpp"
#include "prepare_feature_settings.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

int wmain(int argc,wchar_t** argv){
    try{
        if(argc==2&&std::wstring(argv[1])==L"embedded-hash"){
            std::printf("%s\n",rebirths::prepare::EmbeddedProxyHash().c_str());return 0;
        }
        if(argc<3||argc>4)throw std::runtime_error("usage: preparer-test inspect|settings|prepare|prepare-raw|prepare-adv-cg|prepare-ma123|prepare-large-ma|rollback|uninstall|apply-4gb|restore-exe game [assets]");
        auto game=rebirths::prepare::OpenGame(argv[2]);auto action=std::wstring(argv[1]);
        if(action==L"inspect"){
            std::wprintf(L"game=%u exe=%ls proxy=%ls original=%d ntcore=%d\n",game.id,game.executable.c_str(),game.proxyDirectory.c_str(),game.originalExecutable,game.ntcoreExecutable);return 0;
        }
        if(action==L"settings"){
            auto settings=rebirths::prepare::ReadSettings(game);
            std::printf("profile=%u\n",static_cast<unsigned>(settings.transformProfile));return 0;
        }
        if(action==L"feature-settings"){
            auto settings=rebirths::prepare::ReadSettings(game);
            for(const auto& binding:rebirths::prepare::FeatureBindings)
                std::wprintf(L"%ls=%d\n",rebirths::FeatureSpecFor(binding.id).key,settings.*binding.member);
            return 0;
        }
        if(action==L"prepare"||action==L"prepare-raw"||action==L"prepare-adv-cg"||action==L"prepare-ma123"||action==L"prepare-large-ma"||action==L"prepare-features-off"||action==L"prepare-features-on"){
            auto settings=rebirths::prepare::ReadSettings(game);if(argc==4)settings.assets=argv[3];
            if(action==L"prepare-features-off"||action==L"prepare-features-on")
                for(const auto& binding:rebirths::prepare::FeatureBindings)
                    settings.*binding.member=action==L"prepare-features-on";
            if(action==L"prepare-raw")settings.transformProfile=rebirths::prepare::TransformProfile::Raw;
            if(action==L"prepare-adv-cg"){
                settings.transformProfile=rebirths::prepare::TransformProfile::AdvCgHalf24V1;
                settings.uncompressedAssets=true;
            }
            if(action==L"prepare-ma123")settings.transformProfile=rebirths::prepare::TransformProfile::Rb3Ma123Pilot;
            if(action==L"prepare-large-ma")settings.transformProfile=rebirths::prepare::TransformProfile::Rb3LargeMaPilot;
            rebirths::prepare::PrepareAndInstall(game,settings,[](const rebirths::prepare::Progress& p){
                std::fwprintf(stderr,L"%ls %ls %llu/%llu\n",p.stage.c_str(),p.current.c_str(),p.completed,p.total);
            });
        }else if(action==L"rollback")rebirths::prepare::Rollback(game);
        else if(action==L"uninstall")rebirths::prepare::Uninstall(game);
        else if(action==L"apply-4gb")rebirths::prepare::Apply4GB(game);
        else if(action==L"restore-exe")rebirths::prepare::RestoreOriginalExe(game);
        else throw std::runtime_error("Unknown action");
        std::printf("OK\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
