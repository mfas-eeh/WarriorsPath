#ifndef SHOP_H
#define SHOP_H

#include "raylib.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

struct ShopItem {
    std::string name;
    std::string desc;
    int price = 0;
    std::string iconPath;
    Texture2D icon{};
    bool hasIcon = false;
};

struct Shop {
    int gold = 500;
    bool open = false;
    int selectedIndex = 0;
    float animT = 0.0f;
    std::string feedbackMsg;
    float feedbackTimer = 0.0f;
    Color feedbackColor = GOLD;

    std::vector<ShopItem> items;
    std::function<void(int)> onBuy;

    void Add(const std::string& name, const std::string& desc, int price, const std::string& iconPath = "") {
        ShopItem item;
        item.name = name;
        item.desc = desc;
        item.price = price;
        item.iconPath = iconPath;
        item.hasIcon = false;
        items.push_back(item);
    }

    void LoadTextures(bool crisp = true) {
        for (auto& item : items) {
            if (!item.iconPath.empty()) {
                const char* candidates[4] = {
                    item.iconPath.c_str(),
                    ("../" + item.iconPath).c_str(),
                    ("assets/" + item.iconPath).c_str(),
                    ("../assets/" + item.iconPath).c_str()
                };
                for (const char* c : candidates) {
                    if (FileExists(c)) {
                        Image img = LoadImage(c);
                        if (img.data != nullptr) {
                            item.icon = LoadTextureFromImage(img);
                            if (crisp) SetTextureFilter(item.icon, TEXTURE_FILTER_POINT);
                            else SetTextureFilter(item.icon, TEXTURE_FILTER_BILINEAR);
                            item.hasIcon = (item.icon.id != 0);
                            UnloadImage(img);
                            break;
                        }
                    }
                }
            }
        }
    }

    void Toggle() {
        open = !open;
        if (open) {
            feedbackTimer = 0.0f;
            selectedIndex = 0;
        }
    }

    void Open() {
        open = true;
        feedbackTimer = 0.0f;
        selectedIndex = 0;
    }

    void Close() {
        open = false;
    }

    void Buy(int index) {
        if (index < 0 || index >= (int)items.size()) return;
        auto& item = items[index];
        if (gold >= item.price) {
            gold -= item.price;
            feedbackMsg = "Purchased: " + item.name + "!";
            feedbackColor = Color{ 100, 240, 110, 255 };
            feedbackTimer = 1.6f;
            if (onBuy) onBuy(index);
        } else {
            feedbackMsg = "Not enough gold! (" + std::to_string(item.price - gold) + " more needed)";
            feedbackColor = Color{ 255, 90, 90, 255 };
            feedbackTimer = 1.6f;
        }
    }

    void Update() {
        if (!open) return;
        float dt = GetFrameTime();
        animT += dt;
        if (feedbackTimer > 0.0f) feedbackTimer -= dt;

        // Navigation
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
            selectedIndex = (selectedIndex - 1 + (int)items.size()) % (int)items.size();
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            selectedIndex = (selectedIndex + 1) % (int)items.size();
        }

        // Mouse hover selection & wheel scrolling
        float wheel = GetMouseWheelMove();
        if (wheel > 0.1f) {
            selectedIndex = (selectedIndex - 1 + (int)items.size()) % (int)items.size();
        } else if (wheel < -0.1f) {
            selectedIndex = (selectedIndex + 1) % (int)items.size();
        }

        // Buy with Enter, Space, or E
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_E)) {
            Buy(selectedIndex);
        }

        // Close with ESC
        if (IsKeyPressed(KEY_ESCAPE)) {
            Close();
        }
    }

    void Draw() {
        if (!open) return;
        int sw = GetScreenWidth(), sh = GetScreenHeight();

        // Dark dim backdrop with subtle vignette
        DrawRectangle(0, 0, sw, sh, Color{ 6, 8, 12, 195 });

        // Centered Main Shop Panel
        float panelW = std::min(860.0f, sw - 40.0f);
        float panelH = std::min(560.0f, sh - 40.0f);
        float panelX = (sw - panelW) * 0.5f;
        float panelY = (sh - panelH) * 0.5f;

        // Panel background
        DrawRectangleRounded(Rectangle{ panelX, panelY, panelW, panelH }, 0.05f, 8, Color{ 20, 22, 28, 250 });
        DrawRectangleRoundedLinesEx(Rectangle{ panelX, panelY, panelW, panelH }, 0.05f, 8, 2.0f, Color{ 215, 175, 75, 255 });
        DrawRectangleRoundedLinesEx(Rectangle{ panelX + 3, panelY + 3, panelW - 6, panelH - 6 }, 0.04f, 8, 1.0f, Color{ 70, 55, 25, 180 });

        // Header bar
        DrawRectangleRounded(Rectangle{ panelX + 8, panelY + 8, panelW - 16, 56 }, 0.12f, 6, Color{ 32, 28, 38, 240 });
        DrawRectangleRoundedLinesEx(Rectangle{ panelX + 8, panelY + 8, panelW - 16, 56 }, 0.12f, 6, 1.0f, Color{ 175, 135, 55, 200 });

        const char* title = "THE ALCHEMIST'S STORE";
        DrawText(title, (int)(panelX + 26), (int)(panelY + 16), 22, GOLD);
        DrawText("Rare Potions, Mystical Herbs & Enchanted Blades", (int)(panelX + 26), (int)(panelY + 41), 12, Color{ 190, 195, 210, 200 });

        // Gold pouch indicator
        const char* goldTxt = TextFormat("%d G", gold);
        int gtw = MeasureText(goldTxt, 18);
        float goldBoxW = gtw + 54.0f;
        float goldBoxX = panelX + panelW - goldBoxW - 60.0f;
        DrawRectangleRounded(Rectangle{ goldBoxX, panelY + 16, goldBoxW, 36 }, 0.25f, 6, Color{ 45, 36, 18, 240 });
        DrawRectangleRoundedLinesEx(Rectangle{ goldBoxX, panelY + 16, goldBoxW, 36 }, 0.25f, 6, 1.2f, GOLD);
        DrawCircle((int)(goldBoxX + 18), (int)(panelY + 34), 9.0f, GOLD);
        DrawCircle((int)(goldBoxX + 18), (int)(panelY + 34), 6.5f, Color{ 255, 225, 90, 255 });
        DrawText(goldTxt, (int)(goldBoxX + 34), (int)(panelY + 25), 18, RAYWHITE);

        // Close [X] button
        Rectangle closeBtn = { panelX + panelW - 48.0f, panelY + 16.0f, 36.0f, 36.0f };
        bool closeHover = CheckCollisionPointRec(GetMousePosition(), closeBtn);
        DrawRectangleRounded(closeBtn, 0.25f, 6, closeHover ? Color{ 180, 45, 45, 240 } : Color{ 50, 40, 45, 200 });
        DrawRectangleRoundedLinesEx(closeBtn, 0.25f, 6, 1.0f, closeHover ? RED : RAYWHITE);
        DrawText("X", (int)(closeBtn.x + 12), (int)(closeBtn.y + 8), 20, RAYWHITE);
        if (closeHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Close();
        }

        // Layout: Left column (item list), Right column (selected item details)
        float listW = panelW * 0.52f;
        float listX = panelX + 18.0f;
        float listY = panelY + 76.0f;
        float listH = panelH - 126.0f;

        float itemH = 58.0f;
        float itemGap = 6.0f;

        Vector2 mousePos = GetMousePosition();

        for (size_t i = 0; i < items.size(); ++i) {
            float rowY = listY + i * (itemH + itemGap);
            if (rowY + itemH > listY + listH) break;

            Rectangle rowRec = { listX, rowY, listW, itemH };
            bool isHover = CheckCollisionPointRec(mousePos, rowRec);
            bool isSelected = ((int)i == selectedIndex);

            if (isHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                selectedIndex = (int)i;
            }

            Color bgCol = isSelected ? Color{ 60, 50, 30, 240 } : (isHover ? Color{ 40, 35, 45, 200 } : Color{ 28, 26, 34, 180 });
            Color borderCol = isSelected ? GOLD : (isHover ? Color{ 140, 130, 110, 160 } : Color{ 55, 50, 60, 140 });

            DrawRectangleRounded(rowRec, 0.15f, 6, bgCol);
            DrawRectangleRoundedLinesEx(rowRec, 0.15f, 6, isSelected ? 2.0f : 1.0f, borderCol);

            // Icon box
            Rectangle iconBox = { listX + 8.0f, rowY + 7.0f, 44.0f, 44.0f };
            DrawRectangleRounded(iconBox, 0.2f, 4, Color{ 16, 14, 20, 230 });
            DrawRectangleRoundedLinesEx(iconBox, 0.2f, 4, 1.0f, isSelected ? GOLD : Color{ 60, 60, 75, 180 });

            if (items[i].hasIcon) {
                DrawTexturePro(items[i].icon,
                               Rectangle{ 0, 0, (float)items[i].icon.width, (float)items[i].icon.height },
                               Rectangle{ iconBox.x + 4, iconBox.y + 4, iconBox.width - 8, iconBox.height - 8 },
                               Vector2{ 0, 0 }, 0.0f, WHITE);
            } else {
                DrawCircle((int)(iconBox.x + 22), (int)(iconBox.y + 22), 12.0f, isSelected ? GOLD : Color{ 100, 140, 190, 220 });
            }

            // Name
            DrawText(items[i].name.c_str(), (int)(listX + 62.0f), (int)(rowY + 11.0f), 15, isSelected ? GOLD : RAYWHITE);

            // Short category / desc preview
            DrawText(items[i].desc.c_str(), (int)(listX + 62.0f), (int)(rowY + 32.0f), 11, Color{ 160, 165, 175, 200 });

            // Price tag
            const char* pTxt = TextFormat("%d G", items[i].price);
            int pw = MeasureText(pTxt, 15);
            bool canAfford = (gold >= items[i].price);
            DrawText(pTxt, (int)(listX + listW - pw - 14.0f), (int)(rowY + 20.0f), 15, canAfford ? Color{ 255, 220, 90, 255 } : Color{ 220, 80, 80, 255 });
        }

        // Right column: Item detail card
        float detailX = listX + listW + 18.0f;
        float detailY = listY;
        float detailW = panelW - (listW + 54.0f);
        float detailH = listH;

        Rectangle detailRec = { detailX, detailY, detailW, detailH };
        DrawRectangleRounded(detailRec, 0.08f, 6, Color{ 26, 24, 32, 220 });
        DrawRectangleRoundedLinesEx(detailRec, 0.08f, 6, 1.2f, Color{ 85, 75, 55, 180 });

        if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
            const auto& sel = items[selectedIndex];

            // Preview icon large
            Rectangle bigIconBox = { detailX + (detailW - 80.0f) * 0.5f, detailY + 20.0f, 80.0f, 80.0f };
            DrawRectangleRounded(bigIconBox, 0.18f, 6, Color{ 18, 16, 22, 240 });
            DrawRectangleRoundedLinesEx(bigIconBox, 0.18f, 6, 1.5f, GOLD);

            if (sel.hasIcon) {
                DrawTexturePro(sel.icon,
                               Rectangle{ 0, 0, (float)sel.icon.width, (float)sel.icon.height },
                               Rectangle{ bigIconBox.x + 8, bigIconBox.y + 8, bigIconBox.width - 16, bigIconBox.height - 16 },
                               Vector2{ 0, 0 }, 0.0f, WHITE);
            } else {
                DrawCircle((int)(bigIconBox.x + 40), (int)(bigIconBox.y + 40), 24.0f, GOLD);
            }

            // Item Name
            int tNameW = MeasureText(sel.name.c_str(), 18);
            DrawText(sel.name.c_str(), (int)(detailX + (detailW - tNameW) * 0.5f), (int)(detailY + 114.0f), 18, GOLD);

            // Price badge
            const char* bigPTxt = TextFormat("Price: %d Gold", sel.price);
            int bpw = MeasureText(bigPTxt, 15);
            bool canAfford = (gold >= sel.price);
            DrawText(bigPTxt, (int)(detailX + (detailW - bpw) * 0.5f), (int)(detailY + 140.0f), 15, canAfford ? Color{ 255, 220, 90, 255 } : Color{ 255, 90, 90, 255 });

            // Description box
            Rectangle descBox = { detailX + 16.0f, detailY + 172.0f, detailW - 32.0f, 96.0f };
            DrawRectangleRounded(descBox, 0.12f, 4, Color{ 16, 15, 20, 200 });
            DrawRectangleRoundedLinesEx(descBox, 0.12f, 4, 1.0f, Color{ 55, 50, 65, 140 });
            DrawText(sel.desc.c_str(), (int)(descBox.x + 12), (int)(descBox.y + 14), 13, Color{ 220, 225, 235, 230 });

            // Buy button
            Rectangle buyBtn = { detailX + 24.0f, detailY + detailH - 62.0f, detailW - 48.0f, 44.0f };
            bool buyHover = CheckCollisionPointRec(mousePos, buyBtn);
            Color buyBg = canAfford ? (buyHover ? Color{ 40, 150, 60, 255 } : Color{ 30, 120, 50, 235 })
                                    : (buyHover ? Color{ 90, 40, 40, 240 } : Color{ 60, 30, 30, 200 });
            Color buyBorder = canAfford ? (buyHover ? GREEN : Color{ 140, 230, 150, 200 })
                                        : Color{ 120, 60, 60, 160 };

            DrawRectangleRounded(buyBtn, 0.22f, 6, buyBg);
            DrawRectangleRoundedLinesEx(buyBtn, 0.22f, 6, 1.5f, buyBorder);

            const char* buyLabel = canAfford ? "BUY ITEM [Enter / Click]" : "INSUFFICIENT GOLD";
            int blw = MeasureText(buyLabel, 15);
            DrawText(buyLabel, (int)(buyBtn.x + (buyBtn.width - blw) * 0.5f), (int)(buyBtn.y + 14.0f), 15, RAYWHITE);

            if (buyHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Buy(selectedIndex);
            }
        }

        // Notification feedback message toast
        if (feedbackTimer > 0.0f) {
            float toastAlpha = std::clamp(feedbackTimer / 0.3f, 0.0f, 1.0f);
            int mtw = MeasureText(feedbackMsg.c_str(), 16);
            float toastW = mtw + 48.0f;
            float toastH = 34.0f;
            float toastX = (sw - toastW) * 0.5f;
            float toastY = panelY + panelH - 96.0f;
            DrawRectangleRounded(Rectangle{ toastX, toastY, toastW, toastH }, 0.3f, 6, ColorAlpha(Color{ 10, 12, 18, 240 }, toastAlpha));
            DrawRectangleRoundedLinesEx(Rectangle{ toastX, toastY, toastW, toastH }, 0.3f, 6, 1.5f, ColorAlpha(feedbackColor, toastAlpha));
            DrawText(feedbackMsg.c_str(), (int)(toastX + 24), (int)(toastY + 9), 16, ColorAlpha(feedbackColor, toastAlpha));
        }

        // Footer prompt
        const char* footer = "UP / DOWN / W / S: Navigate   ENTER / SPACE / CLICK: Buy   ESC / X: Return to Store";
        int ftw = MeasureText(footer, 13);
        DrawText(footer, (int)(panelX + (panelW - ftw) * 0.5f), (int)(panelY + panelH - 26.0f), 13, Color{ 160, 165, 175, 200 });
    }

    void Unload() {
        for (auto& item : items) {
            if (item.hasIcon && item.icon.id != 0) {
                UnloadTexture(item.icon);
                item.icon.id = 0;
                item.hasIcon = false;
            }
        }
    }
};

#endif // SHOP_H
