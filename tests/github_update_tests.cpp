#include "github_release.hpp"
#include "updater.hpp"
#include "app_config.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
using namespace vortex;
using nlohmann::json;
void Require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
template<class F>void Reject(F f,const char *message){try{f();}catch(...){return;}throw std::runtime_error(message);}
int main(int argc, char **argv){try {
    if(argc>1 && std::string(argv[1])=="--online") {
        std::atomic<bool> cancel{};
        auto release=GitHubRelease::Parse(FetchGitHubText("https://api.github.com/repos/"+std::string(UpdateUrl).substr(19)+"/releases/latest",2*1024*1024,cancel),UpdateUrl);
        auto sums=FetchGitHubText(release.Asset("SHA256SUMS.txt").url,65536,cancel);
        auto checksums=ParseChecksums(sums);
        auto feed=FetchGitHubText(release.Asset("releases.win.json").url,1024*1024,cancel);
        ValidateReleaseFeed(release,feed,checksums);
        auto directory=std::filesystem::temp_directory_path()/(L"VortexOnlineVerify-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        for(const auto &name:{std::string("VortexSetup.exe"),std::string(AppId)+"-"+release.version+"-full.nupkg"}) {
            auto path=directory/Wide(name);
            DownloadGitHubAsset(release.Asset(name),checksums.at(name),path,cancel,{});
            std::cout<<"Anonymous verified download: "<<name<<" ("<<std::filesystem::file_size(path)<<" bytes)\n";
            std::filesystem::remove(path);
        }
        std::filesystem::remove(directory);
        return 0;
    }

    Require(ReleaseVersion::Parse("1.0.10")>ReleaseVersion::Parse("1.0.9"),"numeric comparison");
    Require(ReleaseVersion::Parse("2.0.0")>ReleaseVersion::Parse("1.99.99"),"major comparison");
    Require(ReleaseVersion::Parse("v3.23.1",true)==ReleaseVersion::Parse("3.23.1"),"tag comparison");
    for(auto v:{"", "1.2", "1.2.3.4", "1.2.03", "-1.2.3", "1.2.3-beta", "1.2.3+build", "65536.0.0", "1.2.3\n", "v1.2.3"})
        Reject([&]{ReleaseVersion::Parse(v);},"invalid version accepted");
    Require(TrustedGitHubUrl("https://release-assets.githubusercontent.com/example?sig=abc"),"CDN HTTPS");
    for(auto u:{"http://github.com/a", "https://github.com.evil.example/a", "https://user:password@github.com/a", "https://github.com:444/a", "https://github.com/a#fragment", "https://other.example/a"})
        Require(!TrustedGitHubUrl(u),"untrusted URL accepted");
    Require(Sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 vector");
    std::string version="3.24.0", package=std::string(AppId)+"-"+version+"-full.nupkg";
    auto packageHash=Sha256("package");
    auto feed=json{{"Assets",json::array({{{"PackageId",AppId},{"Version",version},{"Type","Full"},{"FileName",package},{"Size",7},{"SHA256",packageHash},{"SHA1",""}}})}}.dump();
    auto sums=Sha256("setup")+"  VortexSetup.exe\n"+Sha256(feed)+"  releases.win.json\n"+packageHash+"  "+package+"\n";
    auto assets=json::array();
    for(const auto &[name,bytes]:std::map<std::string,std::string>{{"VortexSetup.exe","setup"},{"releases.win.json",feed},{"SHA256SUMS.txt",sums},{package,"package"}})
        assets.push_back({{"name",name},{"size",bytes.size()},{"state","uploaded"},{"digest","sha256:"+Sha256(bytes)},
                          {"browser_download_url",std::string(UpdateUrl)+"/releases/download/v"+version+"/"+name}});
    json metadata={{"tag_name","v"+version},{"draft",false},{"prerelease",false},{"body","Notes"},{"assets",assets}};
    auto release=GitHubRelease::Parse(metadata.dump(),UpdateUrl);
    auto checksums=ParseChecksums(sums);
    Require(!ValidateReleaseFeed(release,feed,checksums).empty(),"valid feed rejected");
    auto invalid=metadata; invalid["assets"].erase(0);
    Reject([&]{GitHubRelease::Parse(invalid.dump(),UpdateUrl);},"missing asset accepted");
    invalid=metadata;invalid["assets"].push_back(assets[0]);
    Reject([&]{GitHubRelease::Parse(invalid.dump(),UpdateUrl);},"duplicate asset accepted");
    invalid=metadata;invalid["assets"][0]["browser_download_url"]="https://github.com/other/repo/file";
    Reject([&]{GitHubRelease::Parse(invalid.dump(),UpdateUrl);},"foreign repository accepted");
    invalid=metadata;invalid["prerelease"]=true;
    Reject([&]{GitHubRelease::Parse(invalid.dump(),UpdateUrl);},"prerelease accepted");
    Reject([&]{GitHubRelease::Parse("{",UpdateUrl);},"malformed JSON accepted");
    Reject([&]{ValidateReleaseFeed(release,feed+" ",checksums);},"tampered feed accepted");
    auto missing=checksums;missing.erase(package);
    Reject([&]{ValidateReleaseFeed(release,feed,missing);},"missing checksum accepted");
    Reject([&]{ParseChecksums(sums+sums);},"duplicate checksum accepted");
    Reject([&]{ParseChecksums("abcd  file\n");},"invalid checksum accepted");
    std::atomic<bool> cancel{true};
    Reject([&]{FetchGitHubText("https://api.github.com",1024,cancel);},"cancel ignored");
    auto target=std::filesystem::temp_directory_path()/L"vortex-rejected-download-test.bin";
    Reject([&]{DownloadGitHubAsset(release.Asset(package),packageHash,target,cancel,{});},"cancelled file download accepted");
    Require(!std::filesystem::exists(target),"cancelled output retained");
    Updater invalidUpdater("not-a-url"); invalidUpdater.Check();
    Require(!invalidUpdater.Busy() && invalidUpdater.Status().stage==UpdateStage::Error,"invalid configuration must fail safely");
    std::cout<<"GitHub version, metadata, checksums, URL security, cancellation and error-state checks passed.\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
