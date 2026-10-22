#include "prepare_game.hpp"
#include "prepare_ui_adapter.hpp"
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
        if(action==L"extract-plan") {
            auto settings=rebirths::prepare::ReadSettings(game);
            auto result=rebirths::prepare::InspectLooseAssets(game.directory,settings.looseFiles,game.id);
            std::printf("files=%llu bytes=%llu skipped=%llu\n",result.files,result.bytes,result.skipped);return 0;
        }
        if(action==L"ui-actions") {
            rebirths::prepare::UiAdapter adapter; auto view=adapter.Open(argv[2]);
            for(const auto& cap:adapter.Actions(view))
                std::printf("%d enabled=%d confirm=%d\n",int(cap.action),cap.enabled,!cap.confirmation.empty());
            return 0;
        }
        if(action==L"extract-no-confirm" || action==L"extract-ui") {
            rebirths::prepare::UiAdapter adapter;
            preparer::Session session(adapter,[](const auto&,const auto&){throw std::runtime_error("Unexpected launch");});
            session.Select(adapter.Open(argv[2]));
            if(session.Begin(preparer::Action::Extract))throw std::runtime_error("Missing extraction confirmation gate");
            if(action==L"extract-ui") {
                if(!session.Begin(preparer::Action::Extract,true))throw std::runtime_error("Extract action unavailable");
                while(!session.Ready())std::this_thread::yield();
                auto outcome=session.Finish();
                if(outcome.state!=preparer::State::Completed)throw std::runtime_error("Extraction failed");
            }
            std::printf("OK\n");return 0;
        }
        if(action==L"extract" || action==L"extract-cancel" || action==L"extract-stale") {
            auto settings=rebirths::prepare::ReadSettings(game);
            if(action==L"extract-stale")settings.configHash=std::string(64,'0');
            bool stop=false;
            auto result=rebirths::prepare::ExtractAndInstall(game,settings,[&](const rebirths::prepare::Progress& p){
                if(action==L"extract-cancel"&&p.stage==L"Extracting loose files"&&p.completed>=1)stop=true;
            },[&]{return stop;});
            std::printf("files=%llu kept=%llu skipped=%llu\n",result.files,result.reused,result.skipped);return 0;
        }
        if(action==L"ui-features") {
            rebirths::prepare::UiAdapter adapter; auto view = adapter.Open(argv[2]);
            for (auto& f : view.features) std::printf("%s=%d group=%u\n", f.id.c_str(), f.value, f.group);
            return 0;
        }
        if(action==L"install" || action==L"install-stale" || action==L"install-absent-stale" || action==L"install-off") {
            auto settings=rebirths::prepare::ReadSettings(game);
            if(argc==4)settings.assets=argv[3];
            if(action==L"install-stale") settings.configHash=std::string(64, '0');
            if(action==L"install-absent-stale") settings.configHash=std::string();
            if(action==L"install-off")for(const auto& binding:rebirths::prepare::FeatureBindings) settings.*binding.member=false;
            rebirths::prepare::ApplySettings(game,settings); std::printf("OK\n");return 0;
        }
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
