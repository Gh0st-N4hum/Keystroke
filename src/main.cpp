#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <random>
#include <optional>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <ctime>

// ----------------------------------------------------------------------
// Type Duel - MVP
// A 2D typing battle game. You and an opponent "race" to type the same
// random string each round. Whoever finishes first lands an attack,
// scaled by how much faster they were. First to 0 HP loses the round.
// ----------------------------------------------------------------------

enum class GameState
{
    Menu,
    Practice,
    Playing,
    RoundResult,
    GameOver,
    FinalGameOver,
    Victory
};

struct LevelConfig
{
    int level;
    float requiredWPM;  // opponent's fixed typing speed for this level
    int wordDifficulty; // 0 = easy, 1 = medium, 2 = hard
};

std::vector<LevelConfig> buildLevels()
{
    std::vector<LevelConfig> levels;
    float wpmValues[20] = {20, 27, 34, 41, 47, 54, 61, 68, 75, 82,
                           88, 95, 102, 109, 116, 123, 129, 136, 143, 150};
    for (int i = 0; i < 20; i++)
    {
        int diff = (i < 6) ? 0 : (i < 14 ? 1 : 2);
        levels.push_back({i + 1, wpmValues[i], diff});
    }
    return levels;
}

class WordBank
{
public:
    WordBank()
    {
        easy = {
            "the cat sat", "run fast now", "code is fun",
            "type this word", "keep it simple", "win this round"};
        medium = {
            "the quick brown fox jumps", "practice makes perfect every day",
            "typing speed matters a lot", "victory comes to the fastest",
            "focus on accuracy and speed"};
        hard = {
            "the extraordinary programmer conquered every challenge swiftly",
            "consistency and precision define a true typing champion",
            "rapid keystrokes determine the outcome of this duel",
            "only relentless practice separates good from great typists"};
    }

    std::string getRandom(int difficulty)
    {
        std::vector<std::string> *pool =
            (difficulty == 0) ? &easy : (difficulty == 1 ? &medium : &hard);
        std::uniform_int_distribution<size_t> dist(0, pool->size() - 1);
        return (*pool)[dist(rng)];
    }

    // Picks a random phrase from a random difficulty tier - used by Practice mode.
    std::string getRandomAny()
    {
        std::uniform_int_distribution<int> diffDist(0, 2);
        return getRandom(diffDist(rng));
    }

private:
    std::vector<std::string> easy, medium, hard;
    std::mt19937 rng{std::random_device{}()};
};

// Computes words-per-minute given character count and elapsed seconds.
// Standard convention: 1 "word" = 5 characters.
float computeWPM(size_t charCount, float elapsedSeconds)
{
    if (elapsedSeconds <= 0.01f)
        return 0.f;
    float minutes = elapsedSeconds / 60.f;
    return (static_cast<float>(charCount) / 5.f) / minutes;
}

// Wraps text onto multiple lines so it never overflows maxWidth pixels,
// measuring actual rendered width (font metrics aren't monospace).
std::string wrapTextToWidth(const sf::Font &font, const std::string &text, unsigned int charSize, float maxWidth)
{
    std::istringstream words(text);
    std::string word, line, result;
    sf::Text probe(font, "", charSize);
    bool firstLine = true;
    while (words >> word)
    {
        std::string testLine = line.empty() ? word : line + " " + word;
        probe.setString(testLine);
        if (probe.getLocalBounds().size.x > maxWidth && !line.empty())
        {
            if (!firstLine)
                result += "\n";
            result += line;
            firstLine = false;
            line = word;
        }
        else
        {
            line = testLine;
        }
    }
    if (!firstLine)
        result += "\n";
    result += line;
    return result;
}

// ----------------------------------------------------------------------
// Character: wraps a sprite that animates between an Idle loop and a
// one-shot Attack animation, both sliced from horizontal sprite sheets
// with equal-sized frames. Also supports a quick red "hurt" flash.
// ----------------------------------------------------------------------
class Character
{
public:
    Character(const sf::Texture &idleTex, const sf::Texture &attackTex, const sf::Texture &gunTex,
              int frameW, int frameH, int idleFrames, int attackFrames,
              bool flipped, sf::Color tint = sf::Color::White)
        : idleTexture(idleTex), attackTexture(attackTex),
          frameWidth(frameW), frameHeight(frameH),
          idleFrameCount(idleFrames), attackFrameCount(attackFrames),
          flipped(flipped), baseTint(tint), sprite(idleTex), gunSprite(gunTex)
    {
        sprite.setTextureRect(sf::IntRect({0, 0}, {frameWidth, frameHeight}));
        sprite.setOrigin({frameWidth / 2.f, frameHeight / 2.f});
        sprite.setScale({flipped ? -scale : scale, scale});
        sprite.setColor(baseTint);

        sf::Vector2u gunSize = gunTex.getSize();
        gunSprite.setOrigin({0.f, gunSize.y / 2.f});
        gunSprite.setScale({flipped ? -gunScale : gunScale, gunScale});
    }

    void setPosition(sf::Vector2f pos)
    {
        basePosition = pos;
        sprite.setPosition(pos);
        updateGunPosition();
    }

    sf::Vector2f getPosition() const { return basePosition; }

    // World-space position of the gun's barrel tip, where shots originate.
    sf::Vector2f getMuzzlePosition() const
    {
        float dir = flipped ? -1.f : 1.f;
        return {basePosition.x + dir * (gunHandOffsetX + gunTipOffset), basePosition.y + gunHandOffsetY};
    }

    // Starts the one-shot attack animation; auto-returns to Idle when done.
    void playAttack()
    {
        state = State::Attacking;
        currentFrame = 0;
        timer = 0.f;
        sprite.setTexture(attackTexture, true);
        sprite.setTextureRect(sf::IntRect({0, 0}, {frameWidth, frameHeight}));
    }

    // Briefly tints the sprite red to show it took damage.
    void hurtFlash()
    {
        hurtTimer = 0.15f;
    }

    void update(float dt)
    {
        if (hurtTimer > 0.f)
        {
            hurtTimer -= dt;
            sprite.setColor(sf::Color(255, 90, 90));
            if (hurtTimer <= 0.f)
                sprite.setColor(baseTint);
        }

        timer += dt;
        if (state == State::Idle)
        {
            const float idleFrameDuration = 0.15f;
            if (timer >= idleFrameDuration)
            {
                timer -= idleFrameDuration;
                currentFrame = (currentFrame + 1) % idleFrameCount;
                sprite.setTextureRect(sf::IntRect({currentFrame * frameWidth, 0}, {frameWidth, frameHeight}));
            }
        }
        else
        {
            const float attackFrameDuration = 0.06f;
            if (timer >= attackFrameDuration)
            {
                timer -= attackFrameDuration;
                currentFrame++;
                if (currentFrame >= attackFrameCount)
                {
                    state = State::Idle;
                    currentFrame = 0;
                    sprite.setTexture(idleTexture, true);
                    sprite.setTextureRect(sf::IntRect({0, 0}, {frameWidth, frameHeight}));
                }
                else
                {
                    sprite.setTextureRect(sf::IntRect({currentFrame * frameWidth, 0}, {frameWidth, frameHeight}));
                }
            }
        }
        updateGunPosition();
    }

    sf::Sprite sprite;
    sf::Sprite gunSprite;

private:
    void updateGunPosition()
    {
        float dir = flipped ? -1.f : 1.f;
        gunSprite.setPosition({basePosition.x + dir * gunHandOffsetX, basePosition.y + gunHandOffsetY});
    }

    enum class State
    {
        Idle,
        Attacking
    };
    const sf::Texture &idleTexture;
    const sf::Texture &attackTexture;
    int frameWidth, frameHeight;
    int idleFrameCount, attackFrameCount;
    bool flipped;
    sf::Color baseTint;
    State state = State::Idle;
    int currentFrame = 0;
    float timer = 0.f;
    float hurtTimer = 0.f;
    sf::Vector2f basePosition;

    static constexpr float scale = 3.f;
    static constexpr float gunScale = 4.f;
    static constexpr float gunHandOffsetX = 30.f;
    static constexpr float gunHandOffsetY = 12.f;
    static constexpr float gunTipOffset = 20.f;
};

// ----------------------------------------------------------------------
// MuzzleFlash: a short non-looping animation that plays once at the
// gun's barrel tip whenever a shot fires.
// ----------------------------------------------------------------------
struct MuzzleFlash
{
    MuzzleFlash(const sf::Texture &tex, int fw, int fh, int fc, bool flip)
        : sprite(tex), frameW(fw), frameH(fh), frameCount(fc)
    {
        sprite.setTextureRect(sf::IntRect({0, 0}, {frameW, frameH}));
        sprite.setOrigin({frameW / 2.f, frameH / 2.f});
        sprite.setScale({flip ? -2.5f : 2.5f, 2.5f});
    }

    void trigger(sf::Vector2f pos)
    {
        active = true;
        currentFrame = 0;
        timer = 0.f;
        sprite.setPosition(pos);
        sprite.setTextureRect(sf::IntRect({0, 0}, {frameW, frameH}));
    }

    void update(float dt)
    {
        if (!active)
            return;
        timer += dt;
        const float frameDuration = 0.035f;
        if (timer >= frameDuration)
        {
            timer -= frameDuration;
            currentFrame++;
            if (currentFrame >= frameCount)
            {
                active = false;
                return;
            }
            sprite.setTextureRect(sf::IntRect({currentFrame * frameW, 0}, {frameW, frameH}));
        }
    }

    sf::Sprite sprite;
    bool active = false;

private:
    int frameW, frameH, frameCount;
    int currentFrame = 0;
    float timer = 0.f;
};

// ----------------------------------------------------------------------
// Projectile: a bullet that travels from shooter to target over a fixed
// duration, then triggers the target's hurt flash on arrival.
// ----------------------------------------------------------------------
struct Projectile
{
    Projectile(const sf::Texture &tex, sf::Vector2f startPos, sf::Vector2f targetPos,
               float travelDuration, Character *hitTarget)
        : sprite(tex), start(startPos), target(targetPos),
          duration(travelDuration), targetCharacter(hitTarget)
    {
        sf::Vector2u size = tex.getSize();
        sprite.setOrigin({size.x / 2.f, size.y / 2.f});
        sprite.setScale({5.f, 5.f});
        sprite.setPosition(start);
    }

    // Returns false once the projectile has landed and should be removed.
    bool update(float dt)
    {
        elapsed += dt;
        float t = std::min(elapsed / duration, 1.f);
        sprite.setPosition({start.x + (target.x - start.x) * t, start.y + (target.y - start.y) * t});
        if (t >= 1.f)
        {
            if (targetCharacter)
                targetCharacter->hurtFlash();
            return false;
        }
        return true;
    }

    sf::Sprite sprite;

private:
    sf::Vector2f start, target;
    float duration;
    float elapsed = 0.f;
    Character *targetCharacter;
};

// ----------------------------------------------------------------------
// Button: a clickable rectangle with a label, used for all mouse-driven
// navigation (menu, pause overlay, continue prompts) so that keyboard
// letters remain free for typing at all times.
// ----------------------------------------------------------------------
struct Button
{
    Button(const sf::Font &font, const std::string &text, sf::Vector2f pos, sf::Vector2f sz, unsigned int charSize = 20)
        : box(sz), label(font, text, charSize), position(pos), size(sz)
    {
        box.setPosition(pos);
        box.setFillColor(sf::Color(45, 50, 70));
        box.setOutlineColor(sf::Color(140, 150, 190));
        box.setOutlineThickness(2.f);
        label.setPosition({pos.x + 14.f, pos.y + (sz.y - static_cast<float>(charSize)) / 2.f - 4.f});
    }

    void setText(const std::string &text) { label.setString(text); }

    bool contains(sf::Vector2f p) const
    {
        return p.x >= position.x && p.x <= position.x + size.x &&
               p.y >= position.y && p.y <= position.y + size.y;
    }

    void setHover(bool hover)
    {
        box.setFillColor(hover ? sf::Color(70, 78, 105) : sf::Color(45, 50, 70));
    }

    void draw(sf::RenderWindow &win)
    {
        win.draw(box);
        win.draw(label);
    }

    sf::RectangleShape box;
    sf::Text label;
    sf::Vector2f position, size;
};

// ----------------------------------------------------------------------
// LoopingAnim: a simple continuously-looping frame animation, used for
// the victory sequence.
// ----------------------------------------------------------------------
struct LoopingAnim
{
    LoopingAnim(const sf::Texture &tex, int fw, int fh, int fc, float frameDur, float scale)
        : sprite(tex), frameW(fw), frameH(fh), frameCount(fc), frameDuration(frameDur)
    {
        sprite.setTextureRect(sf::IntRect({0, 0}, {frameW, frameH}));
        sprite.setOrigin({frameW / 2.f, frameH / 2.f});
        sprite.setScale({scale, scale});
    }

    void update(float dt)
    {
        timer += dt;
        if (timer >= frameDuration)
        {
            timer -= frameDuration;
            currentFrame = (currentFrame + 1) % frameCount;
            sprite.setTextureRect(sf::IntRect({currentFrame * frameW, 0}, {frameW, frameH}));
        }
    }

    void reset()
    {
        currentFrame = 0;
        timer = 0.f;
        sprite.setTextureRect(sf::IntRect({0, 0}, {frameW, frameH}));
    }

    sf::Sprite sprite;

private:
    int frameW, frameH, frameCount;
    int currentFrame = 0;
    float timer = 0.f;
    float frameDuration;
};

int main()
{
    sf::RenderWindow window(sf::VideoMode({800u, 600u}), "Type Duel", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);

    // --- Font loading with a couple of fallbacks ---
    sf::Font font;
    bool fontLoaded = font.openFromFile("assets/font.ttf");
    if (!fontLoaded)
        fontLoaded = font.openFromFile("C:/Windows/Fonts/consola.ttf");
    if (!fontLoaded)
        fontLoaded = font.openFromFile("C:/Windows/Fonts/arial.ttf");
    if (!fontLoaded)
    {
        // Last resort on Linux dev machines
        fontLoaded = font.openFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }
    if (!fontLoaded)
    {
        // We can still run, but text won't render without a valid font.
        // Drop a font file at assets/font.ttf (see README.md).
    }

    // --- Character sprite sheets ---
    sf::Texture idleTexture, attackTexture, opponentIdleTexture, opponentAttackTexture;
    sf::Texture gunPlayerTexture, gunOpponentTexture, bulletTexture, muzzleFlashTexture;
    bool spritesLoaded = idleTexture.loadFromFile("assets/idle.png");
    spritesLoaded = attackTexture.loadFromFile("assets/attack.png") && spritesLoaded;
    spritesLoaded = opponentIdleTexture.loadFromFile("assets/opponent_idle.png") && spritesLoaded;
    spritesLoaded = opponentAttackTexture.loadFromFile("assets/opponent_attack.png") && spritesLoaded;
    spritesLoaded = gunPlayerTexture.loadFromFile("assets/gun_player.png") && spritesLoaded;
    spritesLoaded = gunOpponentTexture.loadFromFile("assets/gun_opponent.png") && spritesLoaded;
    spritesLoaded = bulletTexture.loadFromFile("assets/bullet.png") && spritesLoaded;
    spritesLoaded = muzzleFlashTexture.loadFromFile("assets/muzzle_flash.png") && spritesLoaded;

    sf::Texture menuBackgroundTexture;
    bool menuBackgroundLoaded = menuBackgroundTexture.loadFromFile("assets/menu_background.png");
    sf::Sprite menuBackgroundSprite(menuBackgroundTexture);
    if (menuBackgroundLoaded)
    {
        sf::Vector2u bgSize = menuBackgroundTexture.getSize();
        menuBackgroundSprite.setScale({800.f / bgSize.x, 600.f / bgSize.y});
    }

    sf::Texture victoryTexture;
    bool victoryLoaded = victoryTexture.loadFromFile("assets/victory.jpg");
    LoopingAnim victoryAnim(victoryTexture, 60, 113, 10, 0.08f, 2.2f);
    victoryAnim.sprite.setPosition({400.f, 220.f});

    sf::RectangleShape menuTextBackdrop({620.f, 160.f});
    menuTextBackdrop.setPosition({90.f, 45.f});
    menuTextBackdrop.setFillColor(sf::Color(20, 20, 30, 140));

    // Frame layout: all character sheets are 48x48 per frame, laid out in one row.
    // idle sheets = 4 frames, attack sheets = 6 frames, muzzle flash = 6 frames.
    Character player(idleTexture, attackTexture, gunPlayerTexture, 48, 48, 4, 6, /*flipped=*/false, sf::Color::White);
    Character opponent(opponentIdleTexture, opponentAttackTexture, gunOpponentTexture, 48, 48, 4, 6, /*flipped=*/true, sf::Color(255, 190, 190));
    player.setPosition({200.f, 205.f});
    opponent.setPosition({600.f, 205.f});

    MuzzleFlash playerMuzzle(muzzleFlashTexture, 48, 48, 6, /*flip=*/false);
    MuzzleFlash opponentMuzzle(muzzleFlashTexture, 48, 48, 6, /*flip=*/true);
    std::vector<Projectile> projectiles;

    WordBank wordBank;
    std::vector<LevelConfig> levels = buildLevels();

    GameState state = GameState::Menu;
    int levelIndex = 0; // 0-based index into levels (1-20)
    int lives = 3;

    float playerHP = 100.f, opponentHP = 100.f;
    std::string targetString;
    std::string typedString;
    sf::Clock roundClock;
    std::string roundMessage;

    auto startRound = [&]()
    {
        targetString = wordBank.getRandom(levels[levelIndex].wordDifficulty);
        typedString.clear();
        roundClock.restart();
    };

    auto startLevel = [&]()
    {
        playerHP = 100.f;
        opponentHP = 100.f;
        startRound();
        state = GameState::Playing;
    };

    float practiceLastWPM = 0.f;
    float practiceBestWPM = 0.f;

    auto startPractice = [&]()
    {
        state = GameState::Practice;
        targetString = wordBank.getRandomAny();
        typedString.clear();
        roundClock.restart();
        practiceLastWPM = 0.f;
        practiceBestWPM = 0.f;
    };

    // --- Pause + session log ---
    bool paused = false;
    std::string pauseFeedback;
    std::vector<std::string> gameLog;

    auto saveLog = [&]()
    {
        std::ofstream file("game_log.txt", std::ios::app);
        if (!file.is_open())
        {
            pauseFeedback = "Failed to save log (could not open game_log.txt).";
            return;
        }
        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        file << "=== Session saved at " << buf << " ===\n";
        for (const auto &line : gameLog)
            file << line << "\n";
        if (state == GameState::Playing || state == GameState::RoundResult)
        {
            file << "Currently on Level " << levels[levelIndex].level << ", Lives: " << lives << "\n";
        }
        file << "\n";
        file.close();
        pauseFeedback = "Log saved to game_log.txt";
    };

    // --- UI text objects ---
    sf::Text titleText(font, "TYPE DUEL", 48);
    titleText.setPosition({230.f, 60.f});

    sf::Text levelText(font, "", 24);
    levelText.setPosition({20.f, 20.f});

    sf::Text targetText(font, "", 26);
    targetText.setPosition({40.f, 300.f});

    sf::Text typedText(font, "", 26);
    typedText.setPosition({40.f, 400.f});

    sf::Text messageText(font, "", 24);
    messageText.setPosition({40.f, 500.f});

    sf::Text footerText(font, "", 18);
    footerText.setPosition({40.f, 560.f});

    sf::Text spriteWarningText(font, "", 16);
    spriteWarningText.setPosition({20.f, 582.f});
    spriteWarningText.setFillColor(sf::Color(255, 100, 100));

    // --- Buttons (mouse-driven navigation, keeps keyboard free for typing) ---
    Button practiceBtn(font, "Practice Mode", {250.f, 220.f}, {300.f, 50.f}, 22);
    Button duelBtn(font, "Start Duel (Lv 1-20)", {250.f, 290.f}, {300.f, 50.f}, 22);

    Button pauseBtn(font, "Pause", {670.f, 15.f}, {110.f, 36.f}, 18);

    Button pauseContinueBtn(font, "Continue", {300.f, 260.f}, {200.f, 46.f}, 20);
    Button pauseSaveBtn(font, "Save Log", {300.f, 320.f}, {200.f, 46.f}, 20);
    Button pauseMenuBtn(font, "Back to Menu", {300.f, 380.f}, {200.f, 46.f}, 20);

    Button continueBtn(font, "Continue", {300.f, 445.f}, {200.f, 44.f}, 20);

    // Player and opponent bars
    sf::RectangleShape playerBarBg({300.f, 24.f});
    playerBarBg.setPosition({50.f, 90.f});
    playerBarBg.setFillColor(sf::Color(60, 60, 60));

    sf::RectangleShape playerBar({300.f, 24.f});
    playerBar.setPosition({50.f, 90.f});
    playerBar.setFillColor(sf::Color(70, 200, 90));

    sf::RectangleShape opponentBarBg({300.f, 24.f});
    opponentBarBg.setPosition({450.f, 90.f});
    opponentBarBg.setFillColor(sf::Color(60, 60, 60));

    sf::RectangleShape opponentBar({300.f, 24.f});
    opponentBar.setPosition({450.f, 90.f});
    opponentBar.setFillColor(sf::Color(210, 70, 70));

    sf::Clock frameClock;

    while (window.isOpen())
    {
        float dt = frameClock.restart().asSeconds();
        if (!paused)
        {
            player.update(dt);
            opponent.update(dt);
            playerMuzzle.update(dt);
            opponentMuzzle.update(dt);
            victoryAnim.update(dt);
            for (auto it = projectiles.begin(); it != projectiles.end();)
            {
                if (!it->update(dt))
                    it = projectiles.erase(it);
                else
                    ++it;
            }
        }

        while (const std::optional<sf::Event> event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }

            if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>())
            {
                if (!paused)
                {
                    if (keyPressed->code == sf::Keyboard::Key::Enter)
                    {
                        if (state == GameState::Menu)
                        {
                            levelIndex = 0;
                            lives = 3;
                            startLevel();
                        }
                        else if (state == GameState::RoundResult)
                        {
                            startRound();
                            state = GameState::Playing;
                        }
                        else if (state == GameState::GameOver)
                        {
                            startLevel(); // lost a life - retry the same level
                        }
                        else if (state == GameState::FinalGameOver)
                        {
                            lives = 3; // out of lives - full reset back to Level 1
                            levelIndex = 0;
                            startLevel();
                        }
                        else if (state == GameState::Victory)
                        {
                            window.close();
                        }
                    }
                    if (keyPressed->code == sf::Keyboard::Key::Escape)
                    {
                        if (state == GameState::Practice)
                        {
                            state = GameState::Menu;
                        }
                        else
                        {
                            window.close();
                        }
                    }
                    if (keyPressed->code == sf::Keyboard::Key::Backspace &&
                        (state == GameState::Playing || state == GameState::Practice) && keyPressed->control)
                    {
                        typedString.clear();
                    }
                }
            }

            if (const auto *textEntered = event->getIf<sf::Event::TextEntered>())
            {
                if (paused)
                {
                    // ignore all typing input while paused
                }
                else if (state == GameState::Practice)
                {
                    unsigned int unicode = textEntered->unicode;
                    if (unicode == 8)
                    {
                        if (!typedString.empty())
                            typedString.pop_back();
                    }
                    else if (unicode >= 32 && unicode < 127)
                    {
                        typedString += static_cast<char>(unicode);
                    }
                    if (typedString == targetString)
                    {
                        float seconds = roundClock.getElapsedTime().asSeconds();
                        practiceLastWPM = computeWPM(targetString.size(), seconds);
                        practiceBestWPM = std::max(practiceBestWPM, practiceLastWPM);
                        targetString = wordBank.getRandomAny();
                        typedString.clear();
                        roundClock.restart();
                    }
                }
                else if (state == GameState::Playing)
                {
                    unsigned int unicode = textEntered->unicode;
                    if (unicode == 8)
                    { // backspace
                        if (!typedString.empty())
                            typedString.pop_back();
                    }
                    else if (unicode >= 32 && unicode < 127)
                    {
                        typedString += static_cast<char>(unicode);
                    }

                    // Check completion
                    if (typedString == targetString)
                    {
                        float playerSeconds = roundClock.getElapsedTime().asSeconds();
                        float playerWPM = computeWPM(targetString.size(), playerSeconds);

                        float requiredWPM = levels[levelIndex].requiredWPM;
                        float opponentSeconds =
                            (targetString.size() / 5.f) / (requiredWPM / 60.f);

                        const float baseDamage = 15.f;
                        if (playerSeconds <= opponentSeconds)
                        {
                            float ratio = std::clamp(opponentSeconds / std::max(playerSeconds, 0.01f), 0.5f, 3.0f);
                            float dmg = baseDamage * ratio;
                            opponentHP -= dmg;
                            player.playAttack();
                            playerMuzzle.trigger(player.getMuzzlePosition());
                            projectiles.emplace_back(bulletTexture, player.getMuzzlePosition(),
                                                     opponent.getPosition(), 0.25f, &opponent);
                            std::ostringstream oss;
                            oss << "You typed at " << static_cast<int>(playerWPM)
                                << " WPM - you strike for " << static_cast<int>(dmg) << " damage!";
                            roundMessage = oss.str();
                        }
                        else
                        {
                            float ratio = std::clamp(playerSeconds / std::max(opponentSeconds, 0.01f), 0.5f, 3.0f);
                            float dmg = baseDamage * ratio;
                            playerHP -= dmg;
                            opponent.playAttack();
                            opponentMuzzle.trigger(opponent.getMuzzlePosition());
                            projectiles.emplace_back(bulletTexture, opponent.getMuzzlePosition(),
                                                     player.getPosition(), 0.25f, &player);
                            std::ostringstream oss;
                            oss << "Too slow (" << static_cast<int>(playerWPM)
                                << " WPM) - opponent strikes for " << static_cast<int>(dmg) << " damage!";
                            roundMessage = oss.str();
                        }

                        opponentHP = std::max(0.f, opponentHP);
                        playerHP = std::max(0.f, playerHP);

                        if (opponentHP <= 0.f)
                        {
                            if (levelIndex == static_cast<int>(levels.size()) - 1)
                            {
                                state = GameState::Victory;
                                victoryAnim.reset();
                                gameLog.push_back("VICTORY! Cleared all 20 levels.");
                            }
                            else
                            {
                                levelIndex++;
                                playerHP = 100.f;
                                opponentHP = 100.f;
                                state = GameState::RoundResult;
                                roundMessage = "Level " + std::to_string(levels[levelIndex - 1].level) + " cleared! Press ENTER for next level.";
                                gameLog.push_back("Cleared Level " + std::to_string(levels[levelIndex - 1].level) + ".");
                            }
                        }
                        else if (playerHP <= 0.f)
                        {
                            lives--;
                            if (lives > 0)
                            {
                                state = GameState::GameOver;
                                gameLog.push_back("Lost a life on Level " + std::to_string(levels[levelIndex].level) +
                                                  " - " + std::to_string(lives) + " lives remaining.");
                            }
                            else
                            {
                                state = GameState::FinalGameOver;
                                gameLog.push_back("Out of lives on Level " + std::to_string(levels[levelIndex].level) +
                                                  " - resetting to Level 1.");
                            }
                        }
                        else
                        {
                            state = GameState::RoundResult;
                        }
                    }
                }
            }

            if (const auto *mousePressed = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (mousePressed->button == sf::Mouse::Button::Left)
                {
                    sf::Vector2f mp = window.mapPixelToCoords(
                        sf::Vector2i(mousePressed->position.x, mousePressed->position.y));

                    if (paused)
                    {
                        if (pauseContinueBtn.contains(mp))
                        {
                            paused = false;
                            roundClock.restart();
                            pauseFeedback.clear();
                        }
                        else if (pauseSaveBtn.contains(mp))
                        {
                            saveLog();
                        }
                        else if (pauseMenuBtn.contains(mp))
                        {
                            paused = false;
                            state = GameState::Menu;
                            pauseFeedback.clear();
                        }
                    }
                    else if (state == GameState::Menu)
                    {
                        if (practiceBtn.contains(mp))
                        {
                            startPractice();
                        }
                        else if (duelBtn.contains(mp))
                        {
                            levelIndex = 0;
                            lives = 3;
                            startLevel();
                        }
                    }
                    else if (state == GameState::Playing || state == GameState::Practice)
                    {
                        if (pauseBtn.contains(mp))
                        {
                            paused = true;
                            pauseFeedback.clear();
                        }
                    }
                    else if (state == GameState::RoundResult)
                    {
                        if (continueBtn.contains(mp))
                        {
                            startRound();
                            state = GameState::Playing;
                        }
                    }
                    else if (state == GameState::GameOver)
                    {
                        if (continueBtn.contains(mp))
                            startLevel();
                    }
                    else if (state == GameState::FinalGameOver)
                    {
                        if (continueBtn.contains(mp))
                        {
                            lives = 3;
                            levelIndex = 0;
                            startLevel();
                        }
                    }
                    else if (state == GameState::Victory)
                    {
                        if (continueBtn.contains(mp))
                            window.close();
                    }
                }
            }
        }

        window.clear(sf::Color(25, 25, 35));

        sf::Vector2f mouseWorld = window.mapPixelToCoords(sf::Mouse::getPosition(window));
        practiceBtn.setHover(practiceBtn.contains(mouseWorld));
        duelBtn.setHover(duelBtn.contains(mouseWorld));
        pauseBtn.setHover(pauseBtn.contains(mouseWorld));
        pauseContinueBtn.setHover(pauseContinueBtn.contains(mouseWorld));
        pauseSaveBtn.setHover(pauseSaveBtn.contains(mouseWorld));
        pauseMenuBtn.setHover(pauseMenuBtn.contains(mouseWorld));
        continueBtn.setHover(continueBtn.contains(mouseWorld));

        if (!spritesLoaded)
        {
            spriteWarningText.setString("WARNING: one or more sprite files failed to load from assets/ - characters will not be visible.");
            window.draw(spriteWarningText);
        }

        if (state == GameState::Menu)
        {
            if (menuBackgroundLoaded)
                window.draw(menuBackgroundSprite);
            window.draw(menuTextBackdrop);
            window.draw(titleText);
            practiceBtn.draw(window);
            duelBtn.draw(window);
        }
        else if (state == GameState::Practice)
        {
            sf::Text practiceTitle(font, "PRACTICE MODE", 30);
            practiceTitle.setPosition({260.f, 40.f});
            window.draw(practiceTitle);

            std::ostringstream statsOss;
            statsOss << "Last: " << static_cast<int>(practiceLastWPM) << " WPM   |   Best: "
                     << static_cast<int>(practiceBestWPM) << " WPM";
            sf::Text practiceStats(font, statsOss.str(), 22);
            practiceStats.setPosition({40.f, 100.f});
            window.draw(practiceStats);

            const float wrapWidth = 720.f;
            targetText.setString(wrapTextToWidth(font, targetString, 26, wrapWidth));
            typedText.setString(wrapTextToWidth(font, typedString, 26, wrapWidth));
            targetText.setPosition({40.f, 220.f});
            typedText.setPosition({40.f, 300.f});
            window.draw(targetText);
            window.draw(typedText);
            targetText.setPosition({40.f, 300.f}); // restore Duel layout position for other states

            footerText.setString("Type the string as fast as you can. Press ESC to return to the menu.");
            footerText.setPosition({40.f, 400.f});
            window.draw(footerText);
            footerText.setPosition({40.f, 560.f}); // restore Duel layout position for other states
            if (!paused)
                pauseBtn.draw(window);
        }
        else
        {
            const LevelConfig &lvl = levels[levelIndex];
            levelText.setString("Level " + std::to_string(lvl.level) + "  |  Opponent speed: " +
                                std::to_string(static_cast<int>(lvl.requiredWPM)) + " WPM  |  Lives: " +
                                std::to_string(lives));
            window.draw(levelText);

            playerBar.setSize({300.f * (playerHP / 100.f), 24.f});
            opponentBar.setSize({300.f * (opponentHP / 100.f), 24.f});
            window.draw(playerBarBg);
            window.draw(playerBar);
            window.draw(opponentBarBg);
            window.draw(opponentBar);

            window.draw(player.sprite);
            window.draw(opponent.sprite);
            window.draw(player.gunSprite);
            window.draw(opponent.gunSprite);
            if (playerMuzzle.active)
                window.draw(playerMuzzle.sprite);
            if (opponentMuzzle.active)
                window.draw(opponentMuzzle.sprite);
            for (auto &proj : projectiles)
                window.draw(proj.sprite);

            const float wrapWidth = 720.f;
            targetText.setString(wrapTextToWidth(font, targetString, 26, wrapWidth));
            typedText.setString(wrapTextToWidth(font, typedString, 26, wrapWidth));
            window.draw(targetText);
            window.draw(typedText);

            if (state == GameState::RoundResult || state == GameState::GameOver || state == GameState::FinalGameOver)
            {
                messageText.setString(wrapTextToWidth(font, roundMessage, 24, wrapWidth));
                window.draw(messageText);

                if (state == GameState::GameOver)
                {
                    footerText.setString("You lost a life! Lives remaining: " + std::to_string(lives) + ".");
                    continueBtn.setText("Retry Level");
                }
                else if (state == GameState::FinalGameOver)
                {
                    footerText.setString("Out of lives! Restarting from Level 1 with 3 fresh lives.");
                    continueBtn.setText("Restart from Level 1");
                }
                else
                {
                    footerText.setString("");
                    continueBtn.setText("Continue");
                }
                window.draw(footerText);
                continueBtn.draw(window);
            }
            else if (state == GameState::Victory)
            {
                if (victoryLoaded)
                    window.draw(victoryAnim.sprite);
                messageText.setString("You beat all 20 levels!");
                window.draw(messageText);
                continueBtn.setText("Exit Game");
                continueBtn.draw(window);
            }
            else
            {
                footerText.setString("Type the string above as fast as you can!");
                window.draw(footerText);
                if (!paused)
                    pauseBtn.draw(window);
            }
        }

        if (paused)
        {
            sf::RectangleShape overlay({800.f, 600.f});
            overlay.setFillColor(sf::Color(10, 10, 15, 190));
            window.draw(overlay);

            sf::Text pauseTitle(font, "PAUSED", 44);
            pauseTitle.setPosition({310.f, 140.f});
            window.draw(pauseTitle);

            pauseContinueBtn.draw(window);
            pauseSaveBtn.draw(window);
            pauseMenuBtn.draw(window);

            sf::Text pauseWarning(font, "(Back to Menu abandons progress in this run)", 16);
            pauseWarning.setPosition({300.f, 430.f});
            pauseWarning.setFillColor(sf::Color(190, 190, 200));
            window.draw(pauseWarning);

            if (!pauseFeedback.empty())
            {
                sf::Text feedback(font, pauseFeedback, 20);
                feedback.setFillColor(sf::Color(120, 230, 140));
                feedback.setPosition({300.f, 460.f});
                window.draw(feedback);
            }
        }

        window.display();
    }

    return 0;
}