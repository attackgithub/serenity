#pragma once

#include "CharacterDevice.h"

class RandomDevice final : public CharacterDevice {
    AK_MAKE_ETERNAL
public:
    RandomDevice();
    virtual ~RandomDevice() override;

    static int random_value();
    static float random_percentage();

private:
    // ^CharacterDevice
    virtual ssize_t read(Process&, byte*, ssize_t) override;
    virtual ssize_t write(Process&, const byte*, ssize_t) override;
    virtual bool can_read(Process&) const override;
    virtual bool can_write(Process&) const override { return true; }
    virtual const char* class_name() const override { return "RandomDevice"; }
};

