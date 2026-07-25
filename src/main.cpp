#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <random>
#include <optional>
#include <algorithm>
#include <cmath>
#include <sstream>

// ----------------------------------------------------------------------
// Type Duel - MVP
// A 2D typing battle game. You and an opponent "race" to type the same
// random string each round. Whoever finishes first lands an attack,
// scaled by how much faster they were. First to 0 HP loses the round.
// ----------------------------------------------------------------------

enum class GameState
{
    Menu,
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
    float wpmValues[10] = {50, 61, 72, 83, 94, 105, 116, 127, 139, 150};
    for (int i = 0; i < 10; i++)
    {
        int diff = (i < 3) ? 0 : (i < 7 ? 1 : 2);
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

int main()
{
    sf::RenderWindow window(sf::VideoMode({800u, 600u}), "Type Duel");
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
    int levelIndex = 0; // 0-based index into levels - also serves as the checkpoint
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

    // --- UI text objects ---
    sf::Text titleText(font, "TYPE DUEL", 48);
    titleText.setPosition({230.f, 60.f});

    sf::Text hintText(font, "Press ENTER to start Level 1", 22);
    hintText.setPosition({210.f, 160.f});

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
        player.update(dt);
        opponent.update(dt);
        playerMuzzle.update(dt);
        opponentMuzzle.update(dt);
        for (auto it = projectiles.begin(); it != projectiles.end();)
        {
            if (!it->update(dt))
                it = projectiles.erase(it);
            else
                ++it;
        }

        while (const std::optional<sf::Event> event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }

            if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>())
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
                        lives = 3; // out of lives - refill and resume from checkpoint (levelIndex)
                        startLevel();
                    }
                    else if (state == GameState::Victory)
                    {
                        window.close();
                    }
                }
                if (keyPressed->code == sf::Keyboard::Key::Escape)
                {
                    window.close();
                }
                if (keyPressed->code == sf::Keyboard::Key::Backspace &&
                    state == GameState::Playing && keyPressed->control)
                {
                    typedString.clear();
                }
            }

            if (const auto *textEntered = event->getIf<sf::Event::TextEntered>())
            {
                if (state == GameState::Playing)
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
                            }
                            else
                            {
                                levelIndex++;
                                playerHP = 100.f;
                                opponentHP = 100.f;
                                state = GameState::RoundResult;
                                roundMessage = "Level " + std::to_string(levels[levelIndex - 1].level) + " cleared! Press ENTER for next level.";
                            }
                        }
                        else if (playerHP <= 0.f)
                        {
                            lives--;
                            if (lives > 0)
                            {
                                state = GameState::GameOver;
                            }
                            else
                            {
                                state = GameState::FinalGameOver;
                            }
                        }
                        else
                        {
                            state = GameState::RoundResult;
                        }
                    }
                }
            }
        }

        window.clear(sf::Color(25, 25, 35));

        if (!spritesLoaded)
        {
            spriteWarningText.setString("WARNING: one or more sprite files failed to load from assets/ - characters will not be visible.");
            window.draw(spriteWarningText);
        }

        if (state == GameState::Menu)
        {
            window.draw(titleText);
            window.draw(hintText);
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
                    footerText.setString("You lost a life! Lives remaining: " + std::to_string(lives) +
                                         ". Press ENTER to retry Level " + std::to_string(lvl.level) + ".");
                }
                else if (state == GameState::FinalGameOver)
                {
                    footerText.setString("Out of lives! Press ENTER to restart from your checkpoint (Level " +
                                         std::to_string(lvl.level) + ") with 3 fresh lives.");
                }
                else
                {
                    footerText.setString("Press ENTER for the next word.");
                }
                window.draw(footerText);
            }
            else if (state == GameState::Victory)
            {
                messageText.setString("You beat all 10 levels! Press ENTER to exit.");
                window.draw(messageText);
            }
            else
            {
                footerText.setString("Type the string above as fast as you can!");
                window.draw(footerText);
            }
        }

        window.display();
    }

    return 0;
}