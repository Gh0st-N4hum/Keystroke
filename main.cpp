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

enum class GameState { Menu, Playing, RoundResult, GameOver, Victory };

struct LevelConfig {
    int level;
    float requiredWPM;   // opponent's fixed typing speed for this level
    int wordDifficulty;  // 0 = easy, 1 = medium, 2 = hard
};

std::vector<LevelConfig> buildLevels() {
    std::vector<LevelConfig> levels;
    float wpmValues[10] = {50, 61, 72, 83, 94, 105, 116, 127, 139, 150};
    for (int i = 0; i < 10; i++) {
        int diff = (i < 3) ? 0 : (i < 7 ? 1 : 2);
        levels.push_back({i + 1, wpmValues[i], diff});
    }
    return levels;
}

class WordBank {
public:
    WordBank() {
        easy = {
            "the cat sat", "run fast now", "code is fun",
            "type this word", "keep it simple", "win this round"
        };
        medium = {
            "the quick brown fox jumps", "practice makes perfect every day",
            "typing speed matters a lot", "victory comes to the fastest",
            "focus on accuracy and speed"
        };
        hard = {
            "the extraordinary programmer conquered every challenge swiftly",
            "consistency and precision define a true typing champion",
            "rapid keystrokes determine the outcome of this duel",
            "only relentless practice separates good from great typists"
        };
    }

    std::string getRandom(int difficulty) {
        std::vector<std::string>* pool =
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
float computeWPM(size_t charCount, float elapsedSeconds) {
    if (elapsedSeconds <= 0.01f) return 0.f;
    float minutes = elapsedSeconds / 60.f;
    return (static_cast<float>(charCount) / 5.f) / minutes;
}

int main() {
    sf::RenderWindow window(sf::VideoMode({800u, 600u}), "Type Duel");
    window.setFramerateLimit(60);

    // --- Font loading with a couple of fallbacks ---
    sf::Font font;
    bool fontLoaded = font.openFromFile("assets/font.ttf");
    if (!fontLoaded) fontLoaded = font.openFromFile("C:/Windows/Fonts/consola.ttf");
    if (!fontLoaded) fontLoaded = font.openFromFile("C:/Windows/Fonts/arial.ttf");
    if (!fontLoaded) {
        // Last resort on Linux dev machines
        fontLoaded = font.openFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }
    if (!fontLoaded) {
        // We can still run, but text won't render without a valid font.
        // Drop a font file at assets/font.ttf (see README.md).
    }

    WordBank wordBank;
    std::vector<LevelConfig> levels = buildLevels();

    GameState state = GameState::Menu;
    int levelIndex = 0; // 0-based index into levels

    float playerHP = 100.f, opponentHP = 100.f;
    std::string targetString;
    std::string typedString;
    sf::Clock roundClock;
    std::string roundMessage;

    auto startRound = [&]() {
        targetString = wordBank.getRandom(levels[levelIndex].wordDifficulty);
        typedString.clear();
        roundClock.restart();
    };

    auto startLevel = [&]() {
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

    sf::Text targetText(font, "", 28);
    targetText.setPosition({50.f, 260.f});

    sf::Text typedText(font, "", 28);
    typedText.setPosition({50.f, 310.f});

    sf::Text messageText(font, "", 26);
    messageText.setPosition({50.f, 400.f});

    sf::Text footerText(font, "", 20);
    footerText.setPosition({50.f, 550.f});

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

    while (window.isOpen()) {
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }

            if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->code == sf::Keyboard::Key::Enter) {
                    if (state == GameState::Menu) {
                        levelIndex = 0;
                        startLevel();
                    } else if (state == GameState::RoundResult) {
                        startRound();
                        state = GameState::Playing;
                    } else if (state == GameState::GameOver) {
                        startLevel(); // retry same level
                    } else if (state == GameState::Victory) {
                        window.close();
                    }
                }
                if (keyPressed->code == sf::Keyboard::Key::Escape) {
                    window.close();
                }
            }

            if (const auto* textEntered = event->getIf<sf::Event::TextEntered>()) {
                if (state == GameState::Playing) {
                    unsigned int unicode = textEntered->unicode;
                    if (unicode == 8) { // backspace
                        if (!typedString.empty()) typedString.pop_back();
                    } else if (unicode >= 32 && unicode < 127) {
                        typedString += static_cast<char>(unicode);
                    }

                    // Check completion
                    if (typedString == targetString) {
                        float playerSeconds = roundClock.getElapsedTime().asSeconds();
                        float playerWPM = computeWPM(targetString.size(), playerSeconds);

                        float requiredWPM = levels[levelIndex].requiredWPM;
                        float opponentSeconds =
                            (targetString.size() / 5.f) / (requiredWPM / 60.f);

                        const float baseDamage = 15.f;
                        if (playerSeconds <= opponentSeconds) {
                            float ratio = std::clamp(opponentSeconds / std::max(playerSeconds, 0.01f), 0.5f, 3.0f);
                            float dmg = baseDamage * ratio;
                            opponentHP -= dmg;
                            std::ostringstream oss;
                            oss << "You typed at " << static_cast<int>(playerWPM)
                                << " WPM - you strike for " << static_cast<int>(dmg) << " damage!";
                            roundMessage = oss.str();
                        } else {
                            float ratio = std::clamp(playerSeconds / std::max(opponentSeconds, 0.01f), 0.5f, 3.0f);
                            float dmg = baseDamage * ratio;
                            playerHP -= dmg;
                            std::ostringstream oss;
                            oss << "Too slow (" << static_cast<int>(playerWPM)
                                << " WPM) - opponent strikes for " << static_cast<int>(dmg) << " damage!";
                            roundMessage = oss.str();
                        }

                        opponentHP = std::max(0.f, opponentHP);
                        playerHP = std::max(0.f, playerHP);

                        if (opponentHP <= 0.f) {
                            if (levelIndex == static_cast<int>(levels.size()) - 1) {
                                state = GameState::Victory;
                            } else {
                                levelIndex++;
                                state = GameState::RoundResult;
                                roundMessage = "Level " + std::to_string(levels[levelIndex - 1].level) + " cleared! Press ENTER for next level.";
                            }
                        } else if (playerHP <= 0.f) {
                            state = GameState::GameOver;
                        } else {
                            state = GameState::RoundResult;
                        }
                    }
                }
            }
        }

        window.clear(sf::Color(25, 25, 35));

        if (state == GameState::Menu) {
            window.draw(titleText);
            window.draw(hintText);
        } else {
            const LevelConfig& lvl = levels[levelIndex];
            levelText.setString("Level " + std::to_string(lvl.level) + "  |  Opponent speed: " +
                                 std::to_string(static_cast<int>(lvl.requiredWPM)) + " WPM");
            window.draw(levelText);

            playerBar.setSize({300.f * (playerHP / 100.f), 24.f});
            opponentBar.setSize({300.f * (opponentHP / 100.f), 24.f});
            window.draw(playerBarBg);
            window.draw(playerBar);
            window.draw(opponentBarBg);
            window.draw(opponentBar);

            targetText.setString(targetString);
            typedText.setString(typedString);
            window.draw(targetText);
            window.draw(typedText);

            if (state == GameState::RoundResult || state == GameState::GameOver) {
                messageText.setString(roundMessage.empty() ? "" : roundMessage);
                window.draw(messageText);
                footerText.setString(state == GameState::GameOver
                    ? "You lost the duel. Press ENTER to retry the level."
                    : "Press ENTER for the next word.");
                window.draw(footerText);
            } else if (state == GameState::Victory) {
                messageText.setString("You beat all 10 levels! Press ENTER to exit.");
                window.draw(messageText);
            } else {
                footerText.setString("Type the string above as fast as you can!");
                window.draw(footerText);
            }
        }

        window.display();
    }

    return 0;
}
