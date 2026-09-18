#pragma once
#include <string>
#include <vector>
namespace kalwer::emoticons {
struct Entry { const char *face, *name, *category, *tags; };
// Built-in catalog from aridlin's wemote. No external process is required.
inline constexpr Entry entries[] = {
    {":-)", "smile", "classic", "happy grin friendly"}, {":)", "smile", "classic", "happy grin friendly"},
    {"=)", "smile", "classic", "happy friendly"}, {":-D", "big grin", "classic", "happy laugh"},
    {":D", "big grin", "classic", "happy laugh"}, {"xD", "laughing", "classic", "lol funny"},
    {";)", "wink", "classic", "flirt playful"}, {";-) ", "wink", "classic", "flirt playful"},
    {":-P", "tongue out", "classic", "silly tease"}, {":P", "tongue out", "classic", "silly tease"},
    {":-p", "tongue out", "classic", "silly tease"}, {":-3", "cat smile", "classic", "cute cat"},
    {":3", "cat smile", "classic", "cute cat"}, {":-]", "friendly", "classic", "happy"},
    {":-}", "friendly", "classic", "happy"}, {":-)", "smile", "classic", "happy"},
    {":-(", "sad", "classic", "unhappy upset"}, {":(", "sad", "classic", "unhappy upset"},
    {"=(", "sad", "classic", "unhappy upset"}, {":-<", "very sad", "classic", "unhappy"},
    {":'(", "crying", "classic", "tears sad"}, {":'-(", "crying", "classic", "tears sad"},
    {"D:", "horror", "classic", "shock scared"}, {">:(", "angry", "classic", "mad rage"},
    {">:-(", "angry", "classic", "mad rage"}, {":-@", "screaming", "classic", "shock angry"},
    {":-O", "surprised", "classic", "shock wow"}, {":O", "surprised", "classic", "shock wow"},
    {":o", "surprised", "classic", "shock wow"}, {":-o", "surprised", "classic", "shock wow"},
    {":-|", "blank", "classic", "neutral bored"}, {":|", "blank", "classic", "neutral bored"},
    {":-/", "skeptical", "classic", "unsure confused"}, {":\\", "skeptical", "classic", "unsure confused"},
    {":-*", "kiss", "classic", "love flirt"}, {":*", "kiss", "classic", "love flirt"},
    {"<3", "heart", "classic", "love"}, {"</3", "broken heart", "classic", "sad love"},
    {";_;", "crying", "anime", "tears sad"}, {"T_T", "crying", "anime", "tears sad"},
    {"T.T", "crying", "anime", "tears sad"}, {"Q_Q", "crying", "anime", "tears sad"},
    {"^_^", "happy", "anime", "smile cute"}, {"^-^", "happy", "anime", "smile cute"},
    {"^.^", "happy", "anime", "smile cute"}, {"^_~", "wink", "anime", "flirt playful"},
    {"o_o", "wide eyes", "anime", "surprised stare"}, {"O_O", "shocked", "anime", "surprised stare"},
    {"0_0", "shocked", "anime", "surprised stare"}, {"-_-", "unimpressed", "anime", "bored annoyed"},
    {"._.", "blank", "anime", "neutral awkward"}, {">_<", "frustrated", "anime", "angry annoyed"},
    {"x_x", "dead", "anime", "knocked out tired"}, {"X_X", "dead", "anime", "knocked out tired"},
    {"@_@", "dizzy", "anime", "confused tired"}, {"$_$", "money eyes", "anime", "greedy cash"},
    {"¬_¬", "side eye", "anime", "skeptical annoyed"}, {"ಥ_ಥ", "heavy crying", "anime", "tears sad"},
    {"ʕ•ᴥ•ʔ", "bear", "cute", "animal happy"}, {"ʕ•́ᴥ•̀ʔ", "worried bear", "cute", "animal sad"},
    {"(=^･ω･^=)", "cat", "cute", "animal kitty"}, {"(=^‥^=)", "cat", "cute", "animal kitty"},
    {"=^_^=", "cat", "cute", "animal kitty happy"}, {"(｡•́︿•̀｡)", "sad", "cute", "crying upset"},
    {"(｡•̀ᴗ-)✧", "sparkle wink", "cute", "happy playful"}, {"(◕‿◕)", "happy", "cute", "smile friendly"},
    {"(◠﹏◠)", "sad", "cute", "unhappy"}, {"(｡◕‿◕｡)", "happy", "cute", "smile friendly"},
    {"(づ｡◕‿‿◕｡)づ", "hug", "cute", "love embrace"}, {"(っ˘з(˘⌣˘ )", "kiss", "cute", "love"},
    {"(╥﹏╥)", "crying", "cute", "tears sad"}, {"(╯°□°）╯︵ ┻━┻", "table flip", "actions", "angry rage"},
    {"┬─┬ ノ( ゜-゜ノ)", "table restore", "actions", "calm"}, {"¯\\_(ツ)_/¯", "shrug", "actions", "unsure whatever"},
    {"(ノಠ益ಠ)ノ彡┻━┻", "table flip", "actions", "angry rage"}, {"(╯°□°)╯︵ ʞooqǝɔɐℲ", "facebook flip", "actions", "angry"},
    {"( •_•)>⌐■-■", "deal with it", "actions", "cool sunglasses"}, {"(⌐■_■)", "deal with it", "actions", "cool sunglasses"},
    {"(╯︵╰,)", "giving up", "actions", "sad"}, {"(ง'̀-'́)ง", "fight", "actions", "angry ready"},
    {"ᕙ(⇀‸↼‶)ᕗ", "flex", "actions", "strong muscles"}, {"ᕕ( ᐛ )ᕗ", "running", "actions", "dance happy"},
    {"ヽ(•‿•)ノ", "yay", "actions", "celebrate happy"}, {"\\o/", "cheer", "actions", "celebrate happy"},
    {"o/", "wave", "actions", "hello goodbye"}, {"(╭☞ ͡° ͜ʖ ͡°)╭☞", "finger guns", "actions", "cool flirt"},
    {"( ͡° ͜ʖ ͡°)", "lenny", "meme", "lewd smug"}, {"( ͠° ͟ʖ ͡°)", "lenny", "meme", "lewd smug"},
    {"( ͡ᵔ ͜ʖ ͡ᵔ )", "cute lenny", "meme", "smug"}, {"(ง ͠° ͟ل͜ ͡°)ง", "fight lenny", "meme", "meme angry"},
    {"༼ つ ◕_◕ ༽つ", "take energy", "meme", "give power"}, {"༼;´༎ຶ ۝ ༎ຶ༽", "weeping", "meme", "crying sad"},
    {"(☞ﾟヮﾟ)☞", "point right", "meme", "finger"}, {"☜(ﾟヮﾟ☜)", "point left", "meme", "finger"},
    {"(☞ﾟ∀ﾟ)☞", "point right", "meme", "finger"}, {"☜(˚▽˚)☞", "point left", "meme", "finger"},
    {"(☞ຈل͜ຈ)☞", "point", "meme", "finger lenny"}, {"(ಥ﹏ಥ)", "crying", "kaomoji", "tears sad"},
    {"(╬ಠ益ಠ)", "furious", "kaomoji", "angry rage"}, {"(≧◡≦)", "delighted", "kaomoji", "happy smile"},
    {"(≧ω≦)", "excited", "kaomoji", "happy"}, {"(≧▽≦)", "very happy", "kaomoji", "happy"},
    {"(¬‿¬)", "smug", "kaomoji", "mischief"}, {"(¬_¬)", "unimpressed", "kaomoji", "annoyed"},
    {"(ʘ‿ʘ)", "surprised", "kaomoji", "wow"}, {"(⊙_⊙)", "staring", "kaomoji", "surprised"},
    {"(•_•)", "awkward", "kaomoji", "blank"}, {"(>_<)", "frustrated", "kaomoji", "angry"},
    {"(－_－) zzZ", "sleeping", "kaomoji", "tired"}, {"(￣﹃￣)", "drooling", "kaomoji", "hungry tired"},
    {"(╯︵╰,)", "crying", "kaomoji", "sad"}, {"(ﾉ◕ヮ◕)ﾉ*:･ﾟ✧", "sparkles", "kaomoji", "celebrate happy"},
};
inline std::string lower(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline std::vector<Entry> search(const std::string& query) {
    std::vector<Entry> out;const auto q=lower(query);
    for(const auto& e:entries) if(lower(std::string(e.face)+" "+e.name+" "+e.category+" "+e.tags).find(q)!=std::string::npos)out.push_back(e);
    return out;
}
}
