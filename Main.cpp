# include <Siv3D.hpp>

// ==========================================
// 1. ゲームの設定（ここをいじるとゲームが変わる！）
// ==========================================
namespace GameConfig
{
    constexpr int32 WindowWidth = 800;
    constexpr int32 WindowHeight = 600;

    constexpr double PlayerY = 500.0;       // 自機のY座標（高さ）
    constexpr double PlayerMaxSpeed = 350.0;// 自機が1秒間に動ける最大スピード
    constexpr double CooldownTime = 0.8;    // レーザーの充電にかかる時間（秒）
}

// ==========================================
// 2. 状態管理とデータの「設計図」（構造体）
// ==========================================

// ゲームの現在の画面を表すリスト
enum class GameState
{
    Title,      // タイトル画面
    Playing,    // プレイ中
    GameOver    // ゲームオーバー画面
};

// 敵キャラクターの設計図
struct Enemy
{
    Vec2 pos;       // 今いる場所（X, Y座標）
    double radius;  // 大きさ（半径）
    int32 corners;  // 何角形か
};

// 爆発の火花の設計図
struct Spark
{
    Vec2 pos;       // 今いる場所
    Vec2 velocity;  // 飛んでいく方向とスピード
    double life;    // 寿命（1.0で誕生、0.0で消滅）
};

// 倒したときにフワッと浮かぶスコア文字の設計図
struct FloatingScore
{
    Vec2 pos;       // 今いる場所
    int32 score;    // 表示する点数
    double life;    // 寿命
};


// ==========================================
// 3. メイン処理（ここからゲームがスタートする）
// ==========================================
void Main()
{
    // ウィンドウの設定
    Window::Resize(GameConfig::WindowWidth, GameConfig::WindowHeight);
    Window::SetTitle(U"Vector Risk");

    // --- ゲームの変数（状態を覚えておく箱） ---
    GameState state = GameState::Title;
    int32 score = 0;
    int32 highScore = 0;
    double playTime = 0.0; // プレイ開始からの経過時間

    // --- プレイヤーの変数 ---
    double playerX = GameConfig::WindowWidth / 2.0; // 最初は真ん中に配置
    double fireTimer = GameConfig::CooldownTime;    // 充電タイマー（最初は満タン）

    // --- キャラクターたちを管理する配列（リスト） ---
    Array<Enemy> enemies;
    Array<Spark> sparks;
    Array<FloatingScore> floatingScores;
    double spawnTimer = 0.0; // 敵を出すためのタイマー

    // --- 描画に使うフォント（文字のスタイル） ---
    const Font fontTitle(60);
    const Font fontScore(30);
    const Font fontPopup(24);

    // --- 効果音（SE）の準備 ---
    // 波の計算式を使って、レトロな電子音を自動生成します
    const Audio seLaser{ Wave(0.1s, Arg::generator = [](double t) {
        return 0.1 * std::sin(Math::TwoPi * (1000.0 * t - 3000.0 * t * t));
    }) };
    const Audio seHit{ Wave(0.2s, Arg::generator = [](double t) {
        return 0.15 * Random(-1.0, 1.0) * (1.0 - t / 0.2);
    }) };
    const Audio seGameOver{ Wave(1.0s, Arg::generator = [](double t) {
        return 0.25 * Random(-1.0, 1.0) * (1.0 - t / 1.0);
    }) };

    // ==========================================
    // メインループ（1秒間に何十回も繰り返される処理）
    // ==========================================
    while (System::Update())
    {
        // 毎フレーム、背景を真っ黒に塗りつぶす（前のフレームの絵を消すため）
        Scene::SetBackground(Palette::Black);

        // --- 共通の更新処理（タイトルでもゲームオーバーでも動かし続けるもの） ---
        
        // Scene::DeltaTime() とは？
        // 「前回のフレームから何秒経ったか（例: 0.016秒）」を教えてくれる魔法の言葉。
        // これを掛け算することで、パソコンの性能に関係なく同じスピードで動くようになります！

        // 1. 火花の移動と寿命管理
        for (auto& spark : sparks)
        {
            spark.pos += spark.velocity * Scene::DeltaTime();
            spark.life -= Scene::DeltaTime() * 1.5; // 少しずつ寿命を減らす
        }
        sparks.remove_if([](const Spark& s) { return s.life <= 0.0; }); // 寿命ゼロで削除

        // 2. ポップアップスコアの移動と寿命管理
        for (auto& fs : floatingScores)
        {
            fs.pos.y -= 50.0 * Scene::DeltaTime(); // 上に向かってフワッと昇る
            fs.life -= Scene::DeltaTime() * 1.0;
        }
        floatingScores.remove_if([](const FloatingScore& fs) { return fs.life <= 0.0; });


        // 画面の状態に合わせて処理を分ける（スイッチ）
        switch (state)
        {
        case GameState::Title:
        {
            // --- 描画処理 ---
            // タイトル文字を緑色と黒色で点滅させる演出
            ColorF titleColor = ColorF{ 0.0, 1.0, 0.0 }.lerp(Palette::Black, Periodic::Sine0_1(1.0s));
            fontTitle(U"VECTOR RISK").drawAt(400, 200, titleColor);
            fontScore(U"Click to Start").drawAt(400, 400, Palette::White);

            // --- 更新処理 ---
            if (MouseL.down())
            {
                // ゲームスタート時の初期化（リセット）
                state = GameState::Playing;
                score = 0;
                enemies.clear();
                sparks.clear();
                floatingScores.clear();
                fireTimer = GameConfig::CooldownTime;
                playTime = 0.0;
                playerX = 400.0; 
            }
            break;
        }

        case GameState::Playing:
        {
            // ------------------------------------------
            // A. 更新処理（内部のデータを計算して動かす）
            // ------------------------------------------
            playTime += Scene::DeltaTime();

            // 1. 自機の移動（マウスに向かって、最高速度以内で近づく）
            double targetX = Cursor::PosF().x;
            double moveAmount = GameConfig::PlayerMaxSpeed * Scene::DeltaTime(); // 今回進める距離

            if (Abs(targetX - playerX) <= moveAmount)
            {
                playerX = targetX; // 近ければピッタリ合わせる
            }
            else
            {
                // 遠ければ、ターゲットの方向に向かって進む
                if (targetX > playerX) { playerX += moveAmount; }
                else { playerX -= moveAmount; }
            }

            // 2. レーザーの発射と充電
            fireTimer += Scene::DeltaTime();
            bool canFire = (fireTimer >= GameConfig::CooldownTime);
            bool isFiring = (canFire && MouseL.down()); // 充電完了 ＋ クリックされたか

            if (isFiring)
            {
                fireTimer = 0.0; // タイマーをリセット（充電開始）
                seLaser.playOneShot();
            }

            // 3. 難易度の計算（時間が経つほど難しくなる！）
            spawnTimer += Scene::DeltaTime();
            double currentSpawnInterval = Max(0.15, 0.5 - (playTime * 0.005)); // 出現間隔（だんだん短く）
            double currentEnemySpeed = 150.0 + (playTime * 3.0);               // 落下速度（だんだん速く）

            // 4. 敵の出現
            if (spawnTimer > currentSpawnInterval)
            {
                // 上空のランダムな位置に、ランダムなサイズの多角形を生成
                enemies.push_back({ Vec2{ Random(50, 750), -50 }, Random(15.0, 30.0), Random(3, 6) });
                spawnTimer = 0.0;
            }

            // 当たり判定用の図形を作る
            Triangle playerTriangle(Vec2{ playerX, GameConfig::PlayerY - 20 }, Vec2{ playerX - 20, GameConfig::PlayerY + 20 }, Vec2{ playerX + 20, GameConfig::PlayerY + 20 });
            RectF laserRect(playerX - 2, 0, 4, GameConfig::PlayerY);

            // 5. 敵の移動と当たり判定（ループしながら処理）
            for (auto it = enemies.begin(); it != enemies.end();)
            {
                it->pos.y += currentEnemySpeed * Scene::DeltaTime();
                Circle enemyCircle(it->pos, it->radius);

                // 【ゲームオーバー判定】自機にぶつかった！
                if (enemyCircle.intersects(playerTriangle))
                {
                    state = GameState::GameOver;
                    if (score > highScore) highScore = score; // ハイスコア更新
                    
                    // 自機の位置から大爆発の火花を100個散らす
                    for (int32 i = 0; i < 100; ++i)
                    {
                        double angle = Random(Math::TwoPi); // 360度ランダム
                        double speed = Random(50.0, 400.0);
                        sparks.push_back({ playerTriangle.centroid(), { Cos(angle) * speed, Sin(angle) * speed }, 1.0 });
                    }

                    seGameOver.playOneShot();
                    break; // ループを抜けてゲームオーバーへ
                }

                // 画面外に落ちた敵は消去
                if (it->pos.y > GameConfig::WindowHeight + it->radius)
                {
                    it = enemies.erase(it);
                    continue;
                }

                // 【撃破判定】レーザーと敵が重なっていて、撃った瞬間か？
                if (isFiring && enemyCircle.intersects(laserRect))
                {
                    // 小爆発の火花を30個散らす
                    for (int32 i = 0; i < 30; ++i)
                    {
                        double angle = Random(Math::TwoPi);
                        double speed = Random(50.0, 250.0);
                        sparks.push_back({ it->pos, { Cos(angle) * speed, Sin(angle) * speed }, 1.0 });
                    }

                    // 【リスクとリターンの要！】Y座標（下）に近いほど高得点！
                    int32 getScore = static_cast<int32>(it->pos.y);
                    score += getScore;
                    
                    // 倒した位置にスコア文字を浮かべる
                    floatingScores.push_back({ it->pos, getScore, 1.0 });
                    seHit.playOneShot();

                    it = enemies.erase(it); // 敵を消去
                }
                else
                {
                    ++it; // 何もなければ次の敵へ
                }
            }

            // ------------------------------------------
            // B. 描画処理（計算されたデータを画面に表示する）
            // ------------------------------------------

            // 1. 火花の描画（加算合成モード）
            // 光が重なると白く輝く表現になります！ネオン風ゲームには必須のテクニックです。
            {
                ScopedRenderStates2D additive(BlendState::Additive);
                for (const auto& spark : sparks)
                {
                    Circle(spark.pos, spark.life * 5.0).draw(ColorF{ 1.0, 0.0, 1.0, spark.life });
                }
            }

            // 2. レーザーの描画（撃った直後の0.15秒間だけ表示してフェードアウト）
            if (fireTimer < 0.15)
            {
                double alpha = 1.0 - (fireTimer / 0.15); // 徐々に透明に
                laserRect.draw(ColorF{ 0.0, 1.0, 1.0, alpha });
            }

            // 3. 自機の描画
            ColorF playerColor = canFire ? Palette::Cyan : Palette::Darkgray;
            playerTriangle.drawFrame(2.0, playerColor);

            // 4. 充電ゲージの描画
            double gaugeRatio = Min(fireTimer / GameConfig::CooldownTime, 1.0);
            ColorF gaugeColor;
            
            // lerp（線形補間）：色と色を、指定した割合（0.0～1.0）で滑らかに混ぜ合わせる魔法！
            if (gaugeRatio < 0.5)
            {
                // 0%～49%は 赤 → 黄色
                gaugeColor = Palette::Red.lerp(Palette::Yellow, gaugeRatio * 2.0);
            }
            else
            {
                // 50%～100%は 黄色 → 緑（ライム）
                gaugeColor = Palette::Yellow.lerp(Palette::Lime, (gaugeRatio - 0.5) * 2.0);
            }
            Line{ playerX - 20, GameConfig::PlayerY + 30, playerX - 20 + 40 * gaugeRatio, GameConfig::PlayerY + 30 }.draw(4.0, gaugeColor);

            // 5. 敵の描画
            for (const auto& enemy : enemies)
            {
                Shape2D::Ngon(enemy.corners, enemy.radius, enemy.pos).drawFrame(2.0, Palette::Magenta);
            }

            // 6. ポップアップスコアの描画
            for (const auto& fs : floatingScores)
            {
                fontPopup(fs.score).drawAt(fs.pos, ColorF{ 1.0, 0.9, 0.2, fs.life });
            }

            // 7. 現在のスコアを画面左上に描画
            fontScore(U"SCORE: ", score).draw(10, 10, Palette::White);
            break;
        }

        case GameState::GameOver:
        {
            // --- 描画処理 ---
            fontTitle(U"GAME OVER").drawAt(400, 200, Palette::Red);
            fontScore(U"Score: ", score).drawAt(400, 300, Palette::White);
            fontScore(U"High Score: ", highScore).drawAt(400, 350, Palette::Yellow);
            fontScore(U"Click to Title").drawAt(400, 500, Palette::Lightgray);

            // ゲームオーバーでも火花やポップアップスコアは最後まで描画してあげる
            {
                ScopedRenderStates2D additive(BlendState::Additive);
                for (const auto& spark : sparks)
                {
                    Circle(spark.pos, spark.life * 5.0).draw(ColorF{ 1.0, 0.0, 1.0, spark.life });
                }
            }
            for (const auto& fs : floatingScores)
            {
                fontPopup(fs.score).drawAt(fs.pos, ColorF{ 1.0, 0.9, 0.2, fs.life });
            }

            // --- 更新処理 ---
            if (MouseL.down())
            {
                state = GameState::Title; // タイトルに戻る
            }
            break;
        }
        }
    }
}