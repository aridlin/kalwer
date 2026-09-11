#include <jni.h>
#include <android/bitmap.h>
#include "games.hpp"
#include "gpu_game.hpp"
#include <mutex>
#include <sstream>
namespace {
JavaVM* vm=nullptr;jclass bridge=nullptr;jmethodID download_method=nullptr,hash_method=nullptr;
std::mutex guard;std::unique_ptr<kalwer::games::Game> game;
kalwer::games::GpuPainter painter;
struct Env {
 JNIEnv* env=nullptr;bool attached=false;
 Env(){if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK){attached=vm->AttachCurrentThread(&env,nullptr)==JNI_OK;}}
 ~Env(){if(attached)vm->DetachCurrentThread();}
};
std::string text(JNIEnv* env,jstring string){if(!string)return {};auto p=env->GetStringChars(string,nullptr);std::u16string value(reinterpret_cast<const char16_t*>(p),env->GetStringLength(string));env->ReleaseStringChars(string,p);auto u=std::filesystem::path(value).u8string();return {u.begin(),u.end()};}
void configure(JNIEnv* env,jclass cls){
 if(bridge)return;env->GetJavaVM(&vm);bridge=static_cast<jclass>(env->NewGlobalRef(cls));
 download_method=env->GetStaticMethodID(cls,"download","(Ljava/lang/String;Ljava/lang/String;)Z");
 hash_method=env->GetStaticMethodID(cls,"sha256","([B)Ljava/lang/String;");
 kalwer::game_assets::download=[](const std::string& url,const std::filesystem::path& path){
  Env attached;auto* e=attached.env;if(!e)return false;
  auto ju=e->NewStringUTF(url.c_str()),jp=e->NewStringUTF(path.string().c_str());
  bool result=e->CallStaticBooleanMethod(bridge,download_method,ju,jp);
  if(e->ExceptionCheck()){e->ExceptionClear();result=false;}e->DeleteLocalRef(ju);e->DeleteLocalRef(jp);return result;
 };
 kalwer::game_assets::hash=[](const std::vector<unsigned char>& bytes){
  Env attached;auto* e=attached.env;if(!e)return std::string{};
  auto array=e->NewByteArray(bytes.size());if(!array){e->ExceptionClear();return std::string{};}
  e->SetByteArrayRegion(array,0,bytes.size(),reinterpret_cast<const jbyte*>(bytes.data()));
  auto value=static_cast<jstring>(e->CallStaticObjectMethod(bridge,hash_method,array));
  std::string result;if(e->ExceptionCheck())e->ExceptionClear();else result=text(e,value);
  e->DeleteLocalRef(value);e->DeleteLocalRef(array);return result;
 };
 kalwer::koom::install_bundle=[](const auto& dir){return kalwer::game_assets::ensure(dir,kalwer::game_assets::freedoom) && kalwer::game_assets::ensure(dir,kalwer::game_assets::soundfont);};
}
}
extern "C" JNIEXPORT void JNICALL Java_pl_aridlin_kalwer_GameNative_open(JNIEnv* env,jclass cls,jint kind,jstring directory,jstring libraries,jlong passive,jint theme,jint opacity,jint dither,jboolean halftone){
 std::lock_guard lock(guard);configure(env,cls);game.reset();
 kalwer::wallet.path=kalwer::koom::utf8_path(text(env,directory))/"koins-v1";kalwer::wallet.load();kalwer::wallet.claim_passive(passive);
 kalwer::koom::runtime_override=kalwer::koom::utf8_path(text(env,libraries))/"libkalwer-koom.so";
 kalwer::appearance.theme=theme;kalwer::appearance.opacity=opacity;kalwer::appearance.popup_mode=dither;kalwer::appearance.popup_keep_halftone=halftone;kalwer::appearance.clamp();
 game=std::make_unique<kalwer::games::Game>(static_cast<kalwer::games::Kind>(std::clamp(int(kind),0,10)));
}
extern "C" JNIEXPORT void JNICALL Java_pl_aridlin_kalwer_GameNative_close(JNIEnv*,jclass){std::lock_guard lock(guard);game.reset();}
extern "C" JNIEXPORT void JNICALL Java_pl_aridlin_kalwer_GameNative_focus(JNIEnv*,jclass,jboolean focused){std::lock_guard lock(guard);if(game)game->focus(focused);}
extern "C" JNIEXPORT void JNICALL Java_pl_aridlin_kalwer_GameNative_key(JNIEnv*,jclass,jint key,jboolean down){std::lock_guard lock(guard);if(game){if(down)game->key(key);else game->release(key);}}
extern "C" JNIEXPORT void JNICALL Java_pl_aridlin_kalwer_GameNative_pointer(JNIEnv*,jclass,jfloat x,jfloat y,jint button){std::lock_guard lock(guard);if(game)game->pointer(x,y,button);}
extern "C" JNIEXPORT jstring JNICALL Java_pl_aridlin_kalwer_GameNative_title(JNIEnv* env,jclass){std::lock_guard lock(guard);return env->NewStringUTF((game?game->title()+"  |  "+std::to_string(kalwer::wallet.balance)+" koins":"").c_str());}
extern "C" JNIEXPORT jint JNICALL Java_pl_aridlin_kalwer_GameNative_kind(JNIEnv*,jclass){std::lock_guard lock(guard);return game?int(game->kind):-1;}
extern "C" JNIEXPORT jstring JNICALL Java_pl_aridlin_kalwer_GameNative_importWad(JNIEnv* env,jclass,jstring path){std::lock_guard lock(guard);auto result=kalwer::koom::import_wad(kalwer::koom::utf8_path(text(env,path)));if(game && game->kind==kalwer::games::Kind::koom)game->reset();return env->NewStringUTF(result.c_str());}
extern "C" JNIEXPORT jstring JNICALL Java_pl_aridlin_kalwer_GameNative_info(JNIEnv* env,jclass){std::lock_guard lock(guard);std::ostringstream out;out<<"{\"kind\":"<<(game?int(game->kind):-1)<<",\"focused\":"<<(game && game->focused?"true":"false")<<",\"elapsed\":"<<(game?game->elapsed:0)<<",\"balance\":"<<kalwer::wallet.balance<<",\"owned\":"<<kalwer::wallet.owned<<",\"frames\":";
 uint64_t frames=0;if(game && game->koom.session){std::lock_guard session_lock(game->koom.session->mutex);frames=game->koom.session->sequence;}out<<frames<<"}";return env->NewStringUTF(out.str().c_str());}
extern "C" JNIEXPORT jboolean JNICALL Java_pl_aridlin_kalwer_GameNative_surface(JNIEnv* env,jclass,jobject bitmap,jfloatArray widths){
 std::lock_guard lock(guard);painter={};AndroidBitmapInfo info{};void* pixels=nullptr;
 if(AndroidBitmap_getInfo(env,bitmap,&info)!=0 || info.width!=512 || info.height!=256 || AndroidBitmap_lockPixels(env,bitmap,&pixels)!=0)return false;
 float advances[96]{};env->GetFloatArrayRegion(widths,0,96,advances);bool okay=painter.init(pixels,advances);AndroidBitmap_unlockPixels(env,bitmap);return okay;
}
extern "C" JNIEXPORT void JNICALL Java_pl_aridlin_kalwer_GameNative_draw(JNIEnv*,jclass,jint width,jint height,jdouble dt){
 std::lock_guard lock(guard);glViewport(0,0,width,height);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
 if(game && painter.program){game->tick(std::clamp(double(dt),0.,.05));game->draw(painter,true);painter.flush();}
}
extern "C" JNIEXPORT jboolean JNICALL Java_pl_aridlin_kalwer_GameNative_unlock(JNIEnv*,jclass){std::lock_guard lock(guard);return kalwer::wallet.unlock_games();}
