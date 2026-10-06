#pragma once
class SwitchGraphicsContext {
public:
 static SwitchGraphicsContext& instance();
 bool initialize(); void shutdown(); bool alive() const; void present();
 int width() const { return 1280; } int height() const { return 720; }
private:
 SwitchGraphicsContext() = default;
 bool alive_ = false;
};
