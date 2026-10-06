#include "platform/Resources.h"
#include "platform/storage/AssetPak.h"
#include <cstdio>
#include <cstdlib>
namespace { const std::string root="sdmc:/switch/OptiCraft/data"; }
std::string PlatformResources::baseDir(){return root;} std::string PlatformResources::assetsDir(){return root+"/assets";} std::string PlatformResources::audioDir(){return root+"/resources";}
std::string PlatformResources::resolveExisting(const std::string& p){ if(AssetPak::mountFrom(root)&&AssetPak::exists(p))return AssetPak::makePath(p); std::string q=root+"/"+p; FILE*f=fopen(q.c_str(),"rb");if(!f)return {};fclose(f);return q; }
std::string PlatformResources::resolveAsset(const std::string& in){std::string p=in;if(!p.empty()&&p[0]=='/')p.erase(0,1);return resolveExisting("assets/"+p);}
long PlatformResources::fileSize(const std::string&p){if(AssetPak::isPakPath(p))return AssetPak::size(AssetPak::keyOf(p));FILE*f=fopen(p.c_str(),"rb");if(!f)return -1;fseek(f,0,SEEK_END);long n=ftell(f);fclose(f);return n;}
unsigned char* PlatformResources::loadFile(const std::string&p,unsigned int*out){if(AssetPak::isPakPath(p))return AssetPak::load(AssetPak::keyOf(p),out);long n=fileSize(p);if(out)*out=0;if(n<=0)return nullptr;auto*d=(unsigned char*)malloc(n);FILE*f=fopen(p.c_str(),"rb");if(!f||fread(d,1,n,f)!=(size_t)n){if(f)fclose(f);free(d);return nullptr;}fclose(f);if(out)*out=(unsigned)n;return d;}
