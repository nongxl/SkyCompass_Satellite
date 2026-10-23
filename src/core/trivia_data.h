#pragma once
#include <Arduino.h>
#include "i18n.h"

struct SatelliteTrivia {
    const char* topic_zh;
    const char* topic_en;
    const char* topic_ja;
    const char* topic_es;
    const char* text_zh;
    const char* text_en;
    const char* text_ja;
    const char* text_es;
};

// 预设精品卫星与航天极客冷知识数据库 (PROGMEM 常量存储)
static const SatelliteTrivia s_satelliteTriviaList[] = {
    // 1. 宇航服卫星
    {
        "太空宇航服卫星", "SPACESUIT SATELLITE", "宇宙服衛星", "TRAJE ESPACIAL",
        "2006年宇航员把一套退役宇航服塞入无线电发射机推入太空，代号SuitSat-1，在轨广播了整整一周语音。",
        "In 2006, astronauts stuffed a radio into a retired spacesuit and tossed it overboard. SuitSat-1 broadcasted voices from orbit for a week.",
        "2006年、宇宙飛行士が無線機を詰めた宇宙服をそのまま放出し誕生したSuitSat-1。1週間にわたり音声を送信しました。",
        "En 2006, astronautas metieron una radio en un traje espacial retirado y lo lanzaron. SuitSat-1 transmitió voces durante una semana."
    },
    // 2. 太空中的圣诞恶作剧
    {
        "太空第一首乐曲", "FIRST SPACE MUSIC", "宇宙初の楽曲", "PRIMERA MÚSICA",
        "1965年双子座6号宇航员谎称发现UFO，随后用偷带上去的口琴与铃铛吹响《铃儿响叮当》，创下太空首次乐器演奏。",
        "In 1965, Gemini 6 astronauts reported a fake UFO sighting, then played 'Jingle Bells' on a smuggled harmonica and bells.",
        "1965年、ジェミニ6号の飛行士がUFO発見と嘘の報告をした後、密かに持ち込んだハーモニカで『ジングルベル』を演奏しました。",
        "En 1965, astronautas de Gemini 6 reportaron un OVNI falso y tocaron 'Jingle Bells' con una armónica que llevaron a escondidas."
    },
    // 3. 卫星耀斑
    {
        "卫星耀斑闪烁", "SATELLITE FLARE", "イリジウムフレア", "DESTELLOS SATELITALES",
        "初代铱星拥有镜面抛光主天线，当角度刚好把阳光反射到地面时，亮度会在几秒内瞬间超过全天最亮的金星！",
        "Original Iridium satellites had mirror-like antennas. When reflecting sunlight to Earth, they briefly outshone Venus!",
        "初代イリジウム衛星は鏡のようなアンテナを持ち、太陽光を反射すると数秒間だけ金星よりも明るく輝きました。",
        "Los satélites Iridium originales tenían antenas pulidas. Al reflejar el sol hacia la Tierra, ¡brillaban más que Venus!"
    },
    // 4. 在太空运行 Python
    {
        "在太空运行 Python", "PYTHON IN SPACE", "宇宙で動くPython", "PYTHON EN EL ESPACIO",
        "PROVES 开源立方星的核心系统直接运行 CircuitPython 代码，高中生也能用几行脚本控制真实的在轨卫星。",
        "PROVES CubeSats run CircuitPython directly onboard. High school students can control a real satellite with Python scripts.",
        "PROVES衛星はCircuitPythonで直接動作しています。高校生でも数行のスクリプトで本物の人工衛星を制御できます。",
        "Los satélites PROVES ejecutan CircuitPython a bordo. Estudiantes pueden controlar un satélite real con simples scripts de Python."
    },
    // 5. 国际空间站需要经常推屁股
    {
        "空间站的大气阻力", "ATMOSPHERIC DRAG", "大気抵抗とISS", "RESISTENCIA EN LA ISS",
        "空间站位于400公里高度，但受微薄大气阻力影响每天下坠约100米，需要货运飞船定期点火把它推回高处。",
        "The ISS orbits in ultra-thin atmosphere, losing ~100m altitude daily. Cargo spacecraft periodically fire engines to push it back up.",
        "ISSは希薄な大気の影響で毎日約100m落下しています。高度を維持するため定期的に補給船がエンジンで押し上げています。",
        "La ISS orbita en atmósfera tenue y cae ~100m al día. Naves de carga encienden sus motores periódicamente para elevarla."
    },
    // 6. 哈勃太空望远镜不用喷气转身
    {
        "反作用飞轮转向", "REACTION WHEELS", "リアクションホイール", "RUEDAS DE REACCIÓN",
        "哈勃望远镜没有任何转向喷气发动机，完全依赖内部几个高速旋转的飞轮利用角动量守恒在太空精准转身。",
        "Hubble has no thrusters for pointing. It rotates entirely by spinning internal reaction wheels to conserve angular momentum.",
        "ハッブル宇宙望遠鏡には方向転換用のスラスターが無く、内部のフライホイールの回転（角運動量保存）だけで向きを変えます。",
        "El Hubble no tiene propulsores para orientarse. Rota girando volantes de inercia internos para conservar el momento angular."
    },
    // 7. 轨道上失落的扳手与工具包
    {
        "高速太空碎片", "SPACE JUNK VELOCITY", "宇宙ゴミの速度", "BASURA ESPACIAL",
        "宇航员曾意外遗落过一把抹刀和一个工具包。在 7.7 km/s 速度下，就算一颗直径1毫米的油漆点都能击穿太空服！",
        "Astronauts have accidentally dropped spatulas and toolbags. At 7.7 km/s, even a 1mm paint fleck packs bullet-like kinetic energy.",
        "宇宙飛行士が工具バッグを誤って落としたことがあります。秒速7.7kmの世界では、1mmの塗料片でも銃弾並みの威力を持ちます。",
        "Astronautas han perdido bolsas de herramientas en el espacio. A 7.7 km/s, ¡una mota de pintura tiene la energía de una bala!"
    },
    // 8. 车库里造出来的业余卫星
    {
        "车库里诞生的奇迹", "GARAGE SATELLITE", "ガレージ製衛星", "SATÉLITE DE GARAJE",
        "1961年业余无线电爱好者在自家庭院车库里组装出 OSCAR-1，成功发射并向全球火腿持续拍发了3个星期的摩尔斯电码“HI”。",
        "In 1961, ham operators built OSCAR-1 in a garage. It successfully launched and sent Morse code 'HI' to Earth for 3 weeks.",
        "1961年、アマチュア無線家が自宅ガレージでOSCAR-1を製作。打ち上げに成功し、世界中にモールス信号「HI」を3週間送信しました。",
        "En 1961, radioaficionados construyeron OSCAR-1 en un garaje. Se lanzó con éxito y transmitió en Morse 'HI' durante 3 semanas."
    },
    // 9. 地球同步轨道的墓地
    {
        "卫星的太空墓地", "GRAVEYARD ORBIT", "静止衛星の墓場", "ÓRBITA CEMENTERIO",
        "地球同步通信卫星燃料耗尽前，会被最后推升到高出常规轨道300公里的“墓地轨道”，在那里静静漂浮数百万年。",
        "Before running out of fuel, geostationary satellites are boosted 300 km higher into a 'graveyard orbit' to drift for millions of years.",
        "静止衛星は燃料が尽きる直前に、通常の300km上空にある「墓場軌道」へ移動させられ、何百万年も漂い続けます。",
        "Antes de quedarse sin combustible, los satélites geoestacionarios suben 300 km a una 'órbita cementerio' donde flotarán millones de años."
    },
    // 10. 旅行者1号的微弱回响
    {
        "暗淡蓝点与远方回响", "VOYAGER 1 SIGNALS", "ボイジャー1号の電波", "SEÑAL DE VOYAGER 1",
        "旅行者1号距地球超240亿公里，其发射机功率仅约20瓦（相当于冰箱灯泡），信号传回地球需要超过22.5小时。",
        "Voyager 1 is over 24 billion km away. Its 20W transmitter (like a fridge bulb) takes over 22.5 hours for signals to reach Earth.",
        "ボイジャー1号は240億km以上彼方にいます。冷蔵庫の電球ほどの20Wの送信機から、電波が地球に届くまで22.5時間以上かかります。",
        "Voyager 1 está a más de 24 mil millones de km. Su transmisor de 20W (como una bombilla) tarda más de 22.5 horas en enviar señales."
    },
    // 11. 什么是多普勒频移？
    {
        "多普勒频移效应", "DOPPLER SHIFT", "ドップラー効果", "EFECTO DOPPLER",
        "卫星向你飞来时射频频率会升高，离你远去时频率会降低。以 437 MHz 为例，进出境频移最高可达近 20 kHz！",
        "As satellites approach, radio frequency shifts higher; receding, it shifts lower. At 437 MHz, this Doppler shift exceeds 18 kHz!",
        "衛星が近づくと電波の周波数は高くなり、遠ざかると低くなります。437MHzでは最大約20kHzものドップラーシフトが発生します。",
        "Cuando un satélite se acerca, su frecuencia sube; al alejarse, baja. ¡A 437 MHz, el efecto Doppler puede superar los 18 kHz!"
    },
    // 12. 为什么低轨卫星大约90分钟绕地球一圈？
    {
        "神奇的90分钟轨道", "90-MINUTE ORBIT", "90分の軌道周期", "ÓRBITA DE 90 MIN",
        "在400公里轨道，引力与离心力平衡所需的轨道速度是 7.66 km/s，绕地球一圈刚好约90分钟，航天员每天看16次日出！",
        "At 400km, orbiting at 7.66 km/s takes just 90 minutes. Astronauts on the ISS experience 16 sunrises and sunsets every single day!",
        "高度400kmを秒速7.66kmで飛ぶと、地球を約90分で1周します。ISSの宇宙飛行士は毎日16回の日の出と日没を体験します。",
        "A 400 km de altura, viajar a 7.66 km/s toma solo 90 minutos. ¡Los astronautas de la ISS viven 16 amaneceres y atardeceres cada día!"
    },
    // 13. 会自愈的深空计算机
    {
        "宇宙射线的比特翻转", "COSMIC BIT FLIPS", "宇宙放射線とメモリ", "RAYOS CÓSMICOS",
        "太空高能宇宙射线会击中芯片让内存比特“0翻转成1”。卫星通常采用三重冗余或自检纠错，让三个处理器同时投票！",
        "Cosmic rays can flip a 0 to 1 in memory. Satellites use Triple Modular Redundancy—three CPUs vote on every calculation to survive.",
        "宇宙放射線がメモリの0と1を反転させることがあります。衛星は3台のCPUで多数決をとるなどして故障を防いでいます。",
        "La radiación cósmica puede voltear un 0 a 1 en la memoria. Los satélites usan tres procesadores que votan cada cálculo para evitar errores."
    },
    // 14. 带着折叠伞飞向深空的先锋
    {
        "太空折纸伞形天线", "SPACE ORIGAMI DISH", "宇宙の折り紙アンテナ", "ORIGAMI ESPACIAL",
        "火箭整流罩空间有限，许多卫星天线被设计成折纸结构，入轨后像雨伞一样撑开到原本直径的数倍大。",
        "Rocket fairings have strict size limits, so large satellite dish antennas fold like origami umbrellas and bloom open in orbit.",
        "ロケットの先端は狭いため、大型衛星アンテナは折り紙のように畳まれ、宇宙に到達した後に傘のように大きく展開します。",
        "El espacio en los cohetes es limitado, por lo que muchas antenas se pliegan como paraguas de origami y se abren en órbita."
    },
    // 15. 地面火腿手台也能通卫星
    {
        "手持对讲机通卫星", "HANDHELD SATELLITE", "ハンディ機での衛星通信", "RADIOAFICIONADOS",
        "不需要巨型抛物面天线，用一部几瓦的普通业余对讲机加自制八木天线，在阳台上就能收到国际空间站广播的声音！",
        "You don't need giant dishes. A 5-watt handheld radio and a DIY Yagi antenna are enough to talk to satellites or the ISS from your balcony!",
        "巨大アンテナは不要です。市販の5Wハンディ無線機と手作り八木アンテナだけで、ベランダからISSの電波を受信できます！",
        "No necesitas antenas gigantes. ¡Con un walkie-talkie de 5W y una antena Yagi casera puedes escuchar a la ISS desde tu balcón!"
    },
    // 16. 月面反射通信
    {
        "月面反射通信", "MOONBOUNCE RADIO", "月面反射通信", "REBOTE LUNAR",
        "在人造卫星诞生前，美军曾向月球发射大功率雷达，利用月岩反射无线电在地球两端通话，被称为月面反射通信(EME)。",
        "Before satellites existed, the US military bounced high-power radar off the Moon's surface to relay voice across Earth (EME).",
        "人工衛星が存在する前、米軍は月面にレーダー波を反射させて地球の裏側と通信する「月面反射通信（EME）」を行っていました。",
        "Antes de los satélites, militares rebotaban señales de radar en la Luna para comunicarse a larga distancia por la Tierra."
    },
    // 17. 手工编织的飞船内存
    {
        "手工编织的飞船内存", "ROPE CORE MEMORY", "手編みの宇宙メモリ", "MEMORIA TEJIDA",
        "阿波罗登月计算机的ROM仅有约64KB，每个比特都是女工用针线把铜线穿过微型磁环手工缝制出来的，被称为“小老太太内存”。",
        "Apollo Guidance Computer ROM was only ~64KB. Every bit was literally hand-woven with copper wires through tiny magnetic rings.",
        "アポロ誘導コンピュータのROMは僅か約64KB。女性工員が手作業で銅線を磁気コアに通して手編みで作られました。",
        "La computadora del Apolo tenía solo ~64KB de ROM. Cada bit fue literalmente tejido a mano pasando hilos de cobre por núcleos."
    },
    // 18. 太空引力平衡点
    {
        "太空引力平衡点", "LAGRANGE PARKING", "ラグランジュ点", "PUNTOS DE LAGRANGE",
        "日地引力与离心力在空间中存在5个完全平衡的“拉格朗日点”，韦伯望远镜就在L2点静静“泊车”，无需动力维持位置。",
        "Five Lagrange points exist where gravity balances out. The James Webb Space Telescope 'parks' at L2 without burning propellant.",
        "太陽と地球の引力が釣り合うラグランジュ点。ジェームズ・ウェッブ望遠鏡はL2点に動力を消費せず静かに停泊しています。",
        "Existen 5 puntos de Lagrange donde la gravedad se equilibra. El telescopio James Webb 'aparca' en L2 sin gastar combustible."
    },
    // 19. 地磁暴与轨道阻力
    {
        "地磁暴与轨道阻力", "GEOMAGNETIC DRAG", "地磁気嵐と衛星", "TORMENTAS SOLARES",
        "2022年一场中等太阳磁暴加热了地球高层大气，导致大气急剧膨胀，新发射的40多颗星链卫星因阻力骤增而在数天内全部坠毁。",
        "In 2022, a solar storm heated Earth's upper atmosphere causing it to expand. Over 40 newly-launched Starlinks fell and burned up.",
        "2022年、太陽活動による地磁気嵐で地球の上層大気が膨張し、打ち上げ直後のスターリンク衛星40基以上が一斉に落下消失しました。",
        "En 2022, una tormenta solar expandió la atmósfera superior. Más de 40 satélites Starlink recién lanzados cayeron por la fricción."
    },
    // 20. 地球的声音金唱片
    {
        "地球的声音金唱片", "VOYAGER GOLDEN RECORD", "ボイジャーの黄金レコード", "DISCO DE ORO",
        "旅行者号探测器携带镀金铜唱片飞向恒星际空间，表面刻有地球的55种语言问候、座头鲸歌声与用脉冲星标定的太阳系地图。",
        "Voyager carries a gold-plated record bearing greetings in 55 languages, whale songs, and a pulsar map locating our Sun in the galaxy.",
        "ボイジャー探査機には55言語の挨拶やクジラの声、パルサーで太陽系を示した地図が刻まれた金メッキレコードが搭載されています。",
        "Las Voyager llevan un disco de oro con saludos en 55 idiomas, cantos de ballenas y un mapa de púlsares para ubicar al Sol."
    },
    // 21. LES-1 幽灵卫星复活 (NORAD 1002)
    {
        "失联幽灵卫星", "GHOST SATELLITE", "迷子衛星の復活", "SATÉLITE FANTASMA",
        "美国LES-1卫星1965年发射后失联数十年。2013年业余无线电爱好者竟重新收到其237MHz电波，太空幽灵开麦了！",
        "Launched in 1965, America's LES-1 satellite reappeared on amateur radio receivers in 2013, transmitting near 237 MHz.",
        "1965年に打ち上げられた米国のLES-1。長年沈黙した後、2013年に約237MHzの信号が再び受信され、宇宙の幽霊が復活した。",
        "Lanzado en 1965, el satélite estadounidense LES-1 reapareció en receptores aficionados en 2013, transmitiendo cerca de 237 MHz."
    },
    // 22. 轨道无线电情报战 (Canyon)
    {
        "太空轨道窃听", "ORBITAL SPYING", "軌道上の盗聴", "ESPIONAJE ORBITAL",
        "冷战期间美国Canyon系列卫星在高轨道侦收苏联微波通信。它们不是拍照间谍，而是给地球的无线电耳朵装上了轨道。",
        "During the Cold War, America's Canyon satellites intercepted Soviet microwave communications from orbit, like giant listening posts.",
        "冷戦期、米国のCanyon衛星は軌道上からソ連のマイクロ波通信を傍受。宇宙に巨大な盗聴器を置く発想だった。",
        "Durante la Guerra Fría, los satélites estadounidenses Canyon interceptaban comunicaciones soviéticas por microondas desde órbita."
    },
    // 23. 萨利尤特7号惊天太空手动对接 (Salyut 7 / Soyuz T-13)
    {
        "礼炮7号太空盲对接", "BLIND DOCKING", "盲目のドッキング", "ACOPLAMIENTO A CIEGAS",
        "1985年苏联礼炮7号失联断电冰冻。宇航员乘联盟T-13号靠近完全失控的空间站，依靠手工盲操作对接，完成人类轨道救援神作！",
        "In 1985, cosmonauts manually docked Soyuz T-13 with the powerless, frozen Salyut 7 station, performing a legendary rescue.",
        "1985年、無電力で制御不能のサリュート7号に、宇宙飛行士がソユーズT-13で接近。手動ドッキングによる前代未聞の救出劇となった。",
        "En 1985, cosmonautas acoplaron manualmente la Soyuz T-13 a la estación Salyut 7 sin energía, en un rescate orbital extraordinario."
    },
    // 24. 火星中继快递员 (Mars Data Relay)
    {
        "火星数据快递员", "MARS DATA RELAY", "火星データ便", "MENSAJERO MARCIANO",
        "火星漫游车无法始终直连地球，而是先把海量数据发给火星轨道器中继。轨道器像太空快递站，跨越数亿公里发回地球。",
        "Mars rovers relay data through orbiters overhead, which forward observations to Earth like interplanetary mail carriers.",
        "火星探査車はデータを軌道周回機へ送り、そこから地球へ転送する。軌道機は惑星間の宇宙郵便局だ。",
        "Los vehículos marcianos suelen enviar datos a orbitadores, que los retransmiten a la Tierra como mensajeros interplanetarios."
    },
    // 25. 空间站慢扫描电视 SSTV (ISS SSTV)
    {
        "空间站轨道传真SSTV", "ISS SSTV POSTCARD", "宇宙からの画像SSTV", "IMÁGENES SSTV",
        "国际空间站宇航员常通过慢扫描电视(SSTV)向地面发图。无线电爱好者用手台加手机解码音频，就能收到来自太空的彩色明信片！",
        "The ISS often transmits images via Slow-Scan TV (SSTV). Ham radio hobbyists can decode these audio chirps into orbital postcards!",
        "国際宇宙ステーションはSSTVで画像を送信してきた。受信機とデコードソフトがあれば、電波から宇宙の絵葉書を復元できる。",
        "La ISS transmite imágenes mediante SSTV. Los aficionados pueden decodificar sus señales de audio y convertirlas en postales espaciales."
    },
    // 26. 行星引力弹弓加速 (Gravity Assist)
    {
        "行星引力借力弹弓", "GRAVITY ASSIST", "惑星スイングバイ", "IMPULSO GRAVITATORIO",
        "航天器掠过运动中的行星时，能偷走极微小公转角动量实现轨道加速，行星损失微乎其微，却能让探测器省下数吨推进剂。",
        "A spacecraft can steal tiny orbital momentum from a moving planet during flybys, gaining huge speed while saving tons of propellant.",
        "探査機は動いている惑星に接近し、軌道エネルギーを交換して太陽基準の速度を変える。推進剤を節約する巧妙な航法だ。",
        "Una sonda puede tomar impulso orbital de un planeta en movimiento durante un sobrevuelo, ganando velocidad y ahorrando combustible."
    },
    // 27. 南大西洋异常区 (South Atlantic Anomaly)
    {
        "辐射百慕大 SAA 异常区", "SOUTH ATLANTIC ANOMALY", "南大西洋の放射線異常帯", "ANOMALÍA DEL ATLÁNTICO SUR",
        "低轨卫星掠过南大西洋上空时，内辐射带高度骤降。高能粒子密集轰击易引发芯片单粒子翻转，哈勃常在此关闭探测器。",
        "When satellites fly over the South Atlantic Anomaly, intense radiation forces instruments like Hubble to shut down to prevent chip errors.",
        "衛星が南大西洋異常帯を通過する際、放射線帯が急低下し高エネルギー粒子が電子機器を襲う。ハッブル望遠鏡も観測を一時停止する。",
        "Al cruzar la Anomalía del Atlántico Sur, la intensa radiación bombardea los chips, obligando a satélites como el Hubble a suspender operaciones."
    },
    // 28. NOAA 极轨气象卫星直播云图 (NOAA APT Weather Imagery)
    {
        "极轨卫星无加密广播云图", "NOAA APT SATELLITE IMAGERY", "NOAA気象衛星の直受画像", "IMÁGENES METEOROLÓGICAS NOAA",
        "NOAA气象卫星在137MHz广播未加密云图。爱好者只需简易天线和SDR接收机，就能在自家阳台实时解调接收地球俯瞰图。",
        "NOAA weather satellites broadcast unencrypted analog cloud imagery on 137MHz. Anyone with an antenna and budget SDR can decode live Earth views.",
        "NOAA気象衛星は137MHzで生の雲画像を暗号化なしで送信している。安価な受信機とアンテナがあれば、自宅で宇宙からの地球画像を復元できる。",
        "Los satélites NOAA transmiten imágenes meteorológicas sin encriptar en 137MHz. Con una antena básica y un SDR se pueden decodificar en directo."
    },
    // 29. 卡西尼号高增益天线充当防尘盾 (Cassini Antenna Shield)
    {
        "高增益天线充当防尘盾", "CASSINI ANTENNA AS SHIELD", "カッシーニのアンテナ盾", "LA ANTENA COMO ESCUDO",
        "卡西尼号高速俯冲土星环缝隙时，地面将其4米大口径天线指向航向充当防尘盾，阻挡微米级环粒子撞击核心仪器。",
        "When plunging through Saturn's ring plane, Cassini rotated its 4-meter high-gain antenna forward as a shield against micro-meteoroid dust.",
        "土星の環の間隙を突入する際、探査機カッシーニは4mの高利得アンテナを進行方向へ向け、微小な氷粒子の衝突から船体を守る盾とした。",
        "Al cruzar los anillos de Saturno, la sonda Cassini orientó su gran antena parabólica de 4 metros hacia adelante como escudo contra micropartículas."
    },
    // 30. 韦伯望远镜与拉格朗日点 (JWST & Sun-Earth L2)
    {
        "拉格朗日点的引力绿洲", "LAGRANGE POINT OASIS", "静穏なラグランジュ点L2", "PUNTO DE LAGRANGE L2",
        "韦伯望远镜驻留在距地150万公里的L2点。日地引力与离心力在此完美平衡，飞船只需极少燃料便能常年在此保持相对静止。",
        "JWST hovers at the Sun-Earth L2 point 1.5 million km away, where gravitational forces balance, requiring minimal fuel for station-keeping.",
        "ジェームズ・ウェッブ望遠鏡は地球から150万km離れたL2点に滞在する。太陽と地球の引力が釣り合い、最小限の燃料で軌道を維持できる。",
        "El telescopio Webb orbita el punto L2 a 1,5 millones de km. Allí las gravedades se equilibran, requiriendo un consumo mínimo de propelente."
    },
    // 31. 深空测控网 DSN 接收微弱信号 (Deep Space Network DSN)
    {
        "深空测控网百亿亿分之一瓦", "DEEP SPACE NETWORK DSN", "極微小信号を受信するDSN", "RED DE ESPACIO PROFUNDO",
        "旅行者1号信号穿越240亿公里抵达地面巨型天线时，微弱至百亿亿分之一瓦，其能量甚至不及一片雪花飘落释放的动能。",
        "Signals from Voyager 1 travel 24 billion km, arriving at giant 70-meter ground dishes with less than an attowatt (10^-18 W) of power.",
        "240億km彼方のボイジャーから届く電波はアトワット級。70m巨大パラボラアンテナが雪片落下よりも微弱な信号を確実に復元する。",
        "Las señales de la Voyager 1 recorren 24.000 millones de km y llegan a antenas de 70 metros con potencias minúsculas del orden de atovatios."
    },
    // 32. 宇宙射线单粒子翻转 (Single Event Upset - SEU)
    {
        "宇宙射线让比特反转", "COSMIC BIT FLIP (SEU)", "宇宙線によるビット反転", "INVERSIÓN DE BITS (SEU)",
        "高能宇宙射线击中芯片可使寄存器由0变1。航天计算机普遍采用三模硬件冗余表决架构，防止单粒子翻转导致飞船失控。",
        "High-energy cosmic rays striking silicon can flip a memory bit from 0 to 1. Spacecraft rely on Triple Modular Redundancy to vote out errors.",
        "高エネルギー宇宙線が半導体に衝突すると0が1へ反転する。宇宙用コンピュータは3重冗余決数回路を採用し、誤作動を瞬時に自己修復する。",
        "Los rayos cósmicos de alta energía pueden invertir bits de 0 a 1 en la memoria. Las naves usan redundancia triple para corregir fallos al vuelo."
    },
    // 33. 太空系绳发电 (Tethered Satellite System - TSS)
    {
        "太空长绳切割地磁发电", "SPACE TETHER GENERATOR", "導電テザー宇宙発電実験", "AMARRA ESPACIAL GENERADORA",
        "航天飞机曾释放近20公里导电绳，高速切割地球磁场感应出超3500伏电压，验证了卫星利用地磁力线无动力发电的可行性。",
        "Space Shuttle experiments trailed a 20-km conducting tether through Earth's magnetic field, generating over 3,500 volts of clean orbital power.",
        "スペースシャトルから約20kmの導電テザーを展開し、地球磁場を切ることで3500V以上の起電力を発生させ、宇宙発電の可能性を実証した。",
        "Un transbordador espacial desplegó un cable conductor de 20 km que cortó el campo magnético terrestre, generando más de 3.500 voltios de electricidad."
    },
    // 34. 卫星离轨帆 (Deorbit Drag Sail)
    {
        "薄膜降落伞让卫星自毁", "DEORBIT DRAG SAIL", "大気突入を促す減速セイル", "VELA DE DESORBITACIÓN",
        "退役微卫星会张开数平米超薄阻力帆，借助热层微弱大气阻力加速轨道衰减，使卫星数年内坠入大气层烧毁，防止沦为太空垃圾。",
        "Retiring smallsats deploy ultra-thin Mylar drag sails. Residual atmospheric friction drastically cuts orbit life, causing clean fiery burnups.",
        "任務を終えた小型衛星は数平方メートルの超薄膜セイルを展開する。超希薄大気の抵抗で軌道降下を加速し、数年で大気圏再突入し燃え尽きる。",
        "Los pequeños satélites despliegan velas de frenado ultraligeras al final de su vida. La fricción atmosférica acelera su reentrada y desintegración."
    },
    // 35. 春秋分的日凌通信中断 (Sun Outage Interference)
    {
        "春秋分的日凌通信中断", "SUN OUTAGE INTERFERENCE", "春分・秋分の太陽雑音障害", "INTERFERENCIA SOLAR",
        "每年春秋分前后，太阳、静止卫星与地面站连成一线。太阳强烈射电噪声彻底压倒卫星信号，导致电视与通信短暂中断数分钟。",
        "During equinoxes, the Sun aligns directly behind geostationary satellites. Solar radio noise temporarily swamps signals, causing brief blackouts.",
        "春分と秋分の頃、太陽・静止衛星・地上局が一直線に並ぶ。強力な太陽電波ノイズが衛星回線を圧倒し、通信や放送が数分間途絶する。",
        "Durante los equinoccios, el Sol se alinea tras los satélites geoestacionarios. El ruido solar satura la señal, provocando cortes temporales."
    },
    // 36. 先锋号异动与热辐射反冲 (Pioneer Anomaly & Thermal Recoil)
    {
        "热辐射反冲与先锋异动", "THE PIONEER ANOMALY", "パイオニア探査機のアノマリー", "LA ANOMALÍA PIONEER",
        "先锋号深空飞行时测得异常减速。谜底在数十年后揭晓：核电池散热不对称产生的光子反冲力，给探测器施加了极微弱的向日加速度。",
        "The mysterious deceleration of Pioneer spacecraft was finally solved: asymmetric thermal radiation from its nuclear generator pushed it back.",
        "パイオニア探査機の謎の減速は長年議論を呼んだ。原因は原子力電池の排热放射の偏りによる、光子圧力のわずかな反作用と判明した。",
        "La misteriosa desaceleración de las sondas Pioneer se debió a la radiación térmica asimétrica de sus generadores, empujando la nave con fotones."
    }
};

class TriviaManager {
public:
    static size_t getCount() {
        return sizeof(s_satelliteTriviaList) / sizeof(s_satelliteTriviaList[0]);
    }

    static const SatelliteTrivia* get(size_t index) {
        if (index >= getCount()) index = 0;
        return &s_satelliteTriviaList[index];
    }

    static const char* getTopic(const SatelliteTrivia* t, Language lang) {
        if (!t) return "";
        switch (lang) {
            case LANG_ZH: return t->topic_zh;
            case LANG_JA: return t->topic_ja;
            case LANG_ES: return t->topic_es;
            case LANG_EN:
            default: return t->topic_en;
        }
    }

    static const char* getText(const SatelliteTrivia* t, Language lang) {
        if (!t) return "";
        switch (lang) {
            case LANG_ZH: return t->text_zh;
            case LANG_JA: return t->text_ja;
            case LANG_ES: return t->text_es;
            case LANG_EN:
            default: return t->text_en;
        }
    }

    static const char* getCardHeader(Language lang) {
        switch (lang) {
            case LANG_ZH: return "你知道吗？";
            case LANG_JA: return "知っていますか？";
            case LANG_ES: return "¿SABÍAS QUE?";
            case LANG_EN:
            default: return "DID YOU KNOW?";
        }
    }
};
