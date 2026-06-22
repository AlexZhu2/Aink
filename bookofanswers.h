#ifndef BOOKOFANSWERS_H
#define BOOKOFANSWERS_H

#include <Arduino.h>
#include <esp_random.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef enum {
  BOOK_ANSWER_POSITIVE = 0,
  BOOK_ANSWER_NEGATIVE,
  BOOK_ANSWER_AMBIGUOUS,
  BOOK_ANSWER_PHILOSOPHY,
  BOOK_ANSWER_CHAOS,
} BookAnswerCategory;

struct BookAnswer {
  const char *mainText;
  const char *subText;
  uint8_t category;
};

static const BookAnswer kBookAnswers[] = {
  {"大吉", "但请先把手机放下", BOOK_ANSWER_POSITIVE},
  {"能成", "宇宙点头幅度约三厘米", BOOK_ANSWER_POSITIVE},
  {"冲吧", "犹豫会被生活收停车费", BOOK_ANSWER_POSITIVE},
  {"有戏", "只是导演还在找光", BOOK_ANSWER_POSITIVE},
  {"可行", "但别把计划写在奶茶杯上", BOOK_ANSWER_POSITIVE},
  {"稳了", "前提是你别临门一脚改需求", BOOK_ANSWER_POSITIVE},
  {"会赢", "先把鞋带系好也算战略", BOOK_ANSWER_POSITIVE},
  {"正合适", "像袜子找到另一只袜子", BOOK_ANSWER_POSITIVE},
  {"好兆头", "风都在帮你翻页", BOOK_ANSWER_POSITIVE},
  {"值得", "钱包皱眉但灵魂鼓掌", BOOK_ANSWER_POSITIVE},
  
  {"必中", "概率已经跪下喊你爸爸", BOOK_ANSWER_POSITIVE},
  {"没问题", "连bug都暂时请假了", BOOK_ANSWER_POSITIVE},
  {"去吧", "Wi‑Fi满格就是天意", BOOK_ANSWER_POSITIVE},
  {"靠谱", "像充电宝还剩99%", BOOK_ANSWER_POSITIVE},
  {"很行", "连你妈都会说一句真香", BOOK_ANSWER_POSITIVE},
  {"上上签", "签王本人都想跟你合影", BOOK_ANSWER_POSITIVE},
  {"稳赚", "哪怕只赚到一个好故事", BOOK_ANSWER_POSITIVE},
  {"放行", "命运安检员给你比OK", BOOK_ANSWER_POSITIVE},
  {"绿灯", "只有这一条车道不堵", BOOK_ANSWER_POSITIVE},
  {"可冲", "风险已经打瞌睡了", BOOK_ANSWER_POSITIVE},
  
  {"先别", "宇宙正在疯狂摇头", BOOK_ANSWER_NEGATIVE},
  {"此路堵", "可能是命运在修地铁", BOOK_ANSWER_NEGATIVE},
  {"不太妙", "建议给冲动按个暂停键", BOOK_ANSWER_NEGATIVE},
  {"换个姿势", "问题没坏，你的打开方式坏了", BOOK_ANSWER_NEGATIVE},
  {"别硬刚", "墙不会疼，你会", BOOK_ANSWER_NEGATIVE},
  {"缓缓", "今天的好运还没起床", BOOK_ANSWER_NEGATIVE},
  {"有坑", "而且坑边还写着欢迎光临", BOOK_ANSWER_NEGATIVE},
  {"撤退", "优雅地跑也是一种智慧", BOOK_ANSWER_NEGATIVE},
  {"别赌", "玄学也讨厌上头", BOOK_ANSWER_NEGATIVE},
  {"不值", "省下的精力可以买快乐", BOOK_ANSWER_NEGATIVE},
  
  {"停手", "现在收手还来得及领奖", BOOK_ANSWER_NEGATIVE},
  {"掉头", "前面只有收费站的尽头", BOOK_ANSWER_NEGATIVE},
  {"算了吧", "连拒绝都温柔得像安慰", BOOK_ANSWER_NEGATIVE},
  {"别试", "好奇心这次真的会咬人", BOOK_ANSWER_NEGATIVE},
  {"劝退", "命运HR说岗位已冻结", BOOK_ANSWER_NEGATIVE},
  {"别闹", "你现在的脑回路在施工", BOOK_ANSWER_NEGATIVE},
  {"危险", "连避雷针都在冒汗", BOOK_ANSWER_NEGATIVE},
  {"驳回", "理由：过于想当然", BOOK_ANSWER_NEGATIVE},
  {"勿扰", "运势正在闭关修炼", BOOK_ANSWER_NEGATIVE},
  {"重练", "这局建议直接删档", BOOK_ANSWER_NEGATIVE},
  
  {"也许吧", "这是最诚实的废话", BOOK_ANSWER_AMBIGUOUS},
  {"看心情", "主要看宇宙的心情", BOOK_ANSWER_AMBIGUOUS},
  {"再等等", "答案正在路上堵车", BOOK_ANSWER_AMBIGUOUS},
  {"半开门", "推一下可能响，踹一下会坏", BOOK_ANSWER_AMBIGUOUS},
  {"有点悬", "但悬着也算一种状态", BOOK_ANSWER_AMBIGUOUS},
  {"未加载", "命运服务器本地缓存中", BOOK_ANSWER_AMBIGUOUS},
  {"不好说", "说了怕你截图", BOOK_ANSWER_AMBIGUOUS},
  {"看缘分", "缘分目前信号两格", BOOK_ANSWER_AMBIGUOUS},
  {"三分天意", "七分靠你别乱点", BOOK_ANSWER_AMBIGUOUS},
  {"问明天", "今天的我只负责神秘", BOOK_ANSWER_AMBIGUOUS},
  
  {"随缘吧", "比努力更佛系的词不多了", BOOK_ANSWER_AMBIGUOUS},
  {"再说", "就像验证码总说请重试", BOOK_ANSWER_AMBIGUOUS},
  {"不一定", "但一定不亏的是经历", BOOK_ANSWER_AMBIGUOUS},
  {"待定", "命运正在开小会", BOOK_ANSWER_AMBIGUOUS},
  {"看情况", "情况正在看你的脸色", BOOK_ANSWER_AMBIGUOUS},
  {"差不多", "误差控制在玄学范围内", BOOK_ANSWER_AMBIGUOUS},
  {"有可能", "概率像余额一样低", BOOK_ANSWER_AMBIGUOUS},
  {"两说", "正面是你，背面也是你", BOOK_ANSWER_AMBIGUOUS},
  {"走着瞧", "瞧着瞧着就习惯了", BOOK_ANSWER_AMBIGUOUS},
  {"看造化", "造化最近比较忙", BOOK_ANSWER_AMBIGUOUS},
  
  {"顺其自然", "但自然不帮你还信用卡", BOOK_ANSWER_PHILOSOPHY},
  {"少即是多", "尤其是未读消息", BOOK_ANSWER_PHILOSOPHY},
  {"放下执念", "先从购物车开始", BOOK_ANSWER_PHILOSOPHY},
  {"慢慢来", "快也不一定显得聪明", BOOK_ANSWER_PHILOSOPHY},
  {"别问结果", "先问自己吃饭了没", BOOK_ANSWER_PHILOSOPHY},
  {"心要稳", "屏幕刷新都比你淡定", BOOK_ANSWER_PHILOSOPHY},
  {"退一步", "不是认输，是调焦", BOOK_ANSWER_PHILOSOPHY},
  {"留白", "墨水屏都懂的高级感", BOOK_ANSWER_PHILOSOPHY},
  {"无为", "但不是不回消息", BOOK_ANSWER_PHILOSOPHY},
  {"在路上", "别催，命运没有加急件", BOOK_ANSWER_PHILOSOPHY},
  
  {"知足", "不然购物车永远清不完", BOOK_ANSWER_PHILOSOPHY},
  {"别较劲", "较劲的结果通常是你疼", BOOK_ANSWER_PHILOSOPHY},
  {"断舍离", "先从聊天记录开始", BOOK_ANSWER_PHILOSOPHY},
  {"平常心", "像对待404页面一样", BOOK_ANSWER_PHILOSOPHY},
  {"别装", "连路由器都知道你在演", BOOK_ANSWER_PHILOSOPHY},
  {"简单点", "复杂留给编译器去烦", BOOK_ANSWER_PHILOSOPHY},
  {"接受", "不接受也得接受加载条", BOOK_ANSWER_PHILOSOPHY},
  {"自洽", "哪怕逻辑像面条也行", BOOK_ANSWER_PHILOSOPHY},
  {"专注", "别一边充电一边耗电", BOOK_ANSWER_PHILOSOPHY},
  {"随它", "它比你懂事多了", BOOK_ANSWER_PHILOSOPHY},
  
  {"重启试试", "这是电子玄学祖训", BOOK_ANSWER_CHAOS},
  {"喝水", "宇宙客服只会这一招", BOOK_ANSWER_CHAOS},
  {"先睡觉", "梦里有免费云计算", BOOK_ANSWER_CHAOS},
  {"问枕头", "它至少不会反驳你", BOOK_ANSWER_CHAOS},
  {"离谱但行", "方案像临时工，居然能跑", BOOK_ANSWER_CHAOS},
  {"发个表情", "语言失败时请启动贴图", BOOK_ANSWER_CHAOS},
  {"买个橘子", "回来答案可能自己熟了", BOOK_ANSWER_CHAOS},
  {"别卷了", "宇宙都开始下班打卡", BOOK_ANSWER_CHAOS},
  {"交给玄学", "但记得保存草稿", BOOK_ANSWER_CHAOS},
  {"先吃饭", "低血糖不适合参透天机", BOOK_ANSWER_CHAOS},
  
  {"随便吧", "反正选择困难症治不好", BOOK_ANSWER_CHAOS},
  {"瞎蒙", "命中率可能比分析高", BOOK_ANSWER_CHAOS},
  {"摆烂", "摆得好叫战略性躺平", BOOK_ANSWER_CHAOS},
  {"听歌", "播放列表比命运算得准", BOOK_ANSWER_CHAOS},
  {"摸鱼", "鱼都上岸了你还在游", BOOK_ANSWER_CHAOS},
  {"关机", "重启人生不需要理由", BOOK_ANSWER_CHAOS},
  {"乱来", "乱来有时候就是创新", BOOK_ANSWER_CHAOS},
  {"发呆", "CPU占用率低也算修行", BOOK_ANSWER_CHAOS},
  {"许愿", "许完记得自己动手实现", BOOK_ANSWER_CHAOS},
  {"抽卡", "非酋也有春天（大概）", BOOK_ANSWER_CHAOS},
};

#define BOOK_ANSWERS_COUNT (sizeof(kBookAnswers) / sizeof(kBookAnswers[0]))

static inline uint32_t bookofanswers_mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  return value;
}

static inline uint32_t bookofanswers_crc32_u32(uint32_t value) {
  uint32_t crc = 0xFFFFFFFFu;
  for (int i = 0; i < 4; i++) {
    crc ^= (value >> (i * 8)) & 0xFFu;
    for (int bit = 0; bit < 8; bit++) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

static inline const BookAnswer *bookofanswers_pick(uint32_t seed, uint16_t *outNumber) {
  const uint32_t mapped = bookofanswers_crc32_u32(bookofanswers_mix32(seed));
  const uint16_t index = (uint16_t)(mapped % BOOK_ANSWERS_COUNT);
  if (outNumber != nullptr) {
    *outNumber = (uint16_t)(index + 1U);
  }
  return &kBookAnswers[index];
}

static inline uint32_t bookofanswers_seed_auto(void) {
  uint32_t seed = esp_random();
  seed ^= (uint32_t)millis() * 2654435761UL;
  seed ^= (uint32_t)time(nullptr);
  seed ^= bookofanswers_mix32((uint32_t)micros());
  return bookofanswers_mix32(seed);
}

static inline uint32_t bookofanswers_seed_from_image_bytes(const uint8_t *data, size_t len) {
  uint32_t hist[16] = {};
  if (data != nullptr) {
    for (size_t i = 0; i < len; i++) {
      hist[(data[i] >> 4) & 0x0F]++;
    }
  }

  uint32_t imageHash = 0xA17B00A5u;
  for (int i = 0; i < 16; i++) {
    imageHash = imageHash * 31U + hist[i];
  }

  uint32_t seed = imageHash;
  seed ^= esp_random();
  seed ^= (uint32_t)micros();
  seed ^= (uint32_t)len * 2246822519UL;
  return bookofanswers_mix32(seed);
}

static inline const char *bookofanswers_random(void) {
  uint16_t number = 0;
  return bookofanswers_pick(esp_random(), &number)->mainText;
}

static inline const char *bookofanswers_random_timed(void) {
  uint16_t number = 0;
  return bookofanswers_pick(bookofanswers_seed_auto(), &number)->mainText;
}

#endif
