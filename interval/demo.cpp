#include <iomanip>
#include <iostream>

#include "interval.hxx"

// ─── helpers ──────────────────────────────────────────────────────────────────
static void section(const char* title) {
    std::cout << "\n╔══════════════════════════════════════╗\n";
    std::cout << "  " << title << "\n";
    std::cout << "╚══════════════════════════════════════╝\n";
}

// ─── 1. sensor threshold monitoring ──────────────────────────────────────────
void demo_sensor() {
    section("Sensor Threshold Monitoring");

    const Interval<float> safe_temp{10.f, 85.f};
    const Interval<float> warn_temp{0.f, 95.f};

    const float readings[] = {-5.f, 8.f, 45.f, 88.f, 102.f};

    for (float temp : readings) {
        std::string status;
        if (!warn_temp.contains(temp))
            status = "CRITICAL";
        else if (!safe_temp.contains(temp))
            status = "WARNING ";
        else
            status = "OK      ";
        std::cout << "  Temp " << std::setw(6) << temp << "°C  →  " << status << "\n";
    }
}

// ─── 2. animation blending (normalize / denormalize) ─────────────────────────
void demo_animation() {
    section("Animation Blending");

    // keyframe timestamps in milliseconds
    const Interval<float> clip{200.f, 800.f};

    const float timestamps[] = {200.f, 350.f, 500.f, 650.f, 800.f};

    std::cout << "  Clip range: [" << clip.min() << "ms, " << clip.max() << "ms]\n\n";
    for (float t : timestamps) {
        float norm = clip.normalize(t);
        std::cout << "  t=" << std::setw(5) << t << "ms  →  norm=" << std::fixed << std::setprecision(2) << norm
                  << "  →  back=" << clip.denormalize(norm) << "ms\n";
    }
}

// ─── 3. UI slider with clamped input ─────────────────────────────────────────
void demo_slider() {
    section("UI Slider Clamping");

    const Interval<int> slider{0, 100};

    const int raw_inputs[] = {-20, 0, 42, 100, 150};

    for (int raw : raw_inputs) {
        std::cout << "  raw=" << std::setw(4) << raw << "  →  clamped=" << slider.clamp(raw) << "\n";
    }
}

// ─── 4. range overlap (scheduling / collision) ───────────────────────────────
void demo_scheduling() {
    section("Meeting Overlap Detection");

    // hours of the day as integers
    struct Meeting {
        const char* name;
        Interval<int> time;
    };
    Meeting meetings[] = {
        {"Standup", {9, 10}}, {"Design", {10, 12}}, {"Lunch", {12, 13}}, {"Review", {11, 13}}, {"Planning", {14, 16}},
    };

    int n = sizeof(meetings) / sizeof(meetings[0]);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (meetings[i].time.overlaps(meetings[j].time)) {
                auto overlap = meetings[i].time.intersect(meetings[j].time);
                std::cout << "  CONFLICT: \"" << meetings[i].name << "\" and \"" << meetings[j].name << "\" overlap at [" << overlap->min() << "h, "
                          << overlap->max() << "h]\n";
            }
        }
    }
}

// ─── 5. merge and expand ─────────────────────────────────────────────────────
void demo_merge_expand() {
    section("Bounding Box Merge & Expand");

    // imagine these are 1D bounding intervals of objects in a scene
    Interval<float> obj_a{1.f, 4.f};
    Interval<float> obj_b{3.f, 7.f};
    Interval<float> obj_c{9.f, 12.f};

    auto ab = obj_a.merge(obj_b);
    auto all = ab.merge(obj_c);
    auto padded = all.expand(1.f);

    std::cout << "  obj_a:   [" << obj_a.min() << ", " << obj_a.max() << "]\n";
    std::cout << "  obj_b:   [" << obj_b.min() << ", " << obj_b.max() << "]\n";
    std::cout << "  obj_c:   [" << obj_c.min() << ", " << obj_c.max() << "]\n";
    std::cout << "  a∪b:     [" << ab.min() << ", " << ab.max() << "]\n";
    std::cout << "  a∪b∪c:   [" << all.min() << ", " << all.max() << "]\n";
    std::cout << "  padded:  [" << padded.min() << ", " << padded.max() << "]  (±1 margin)\n";
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    demo_sensor();
    demo_animation();
    demo_slider();
    demo_scheduling();
    demo_merge_expand();

    std::cout << "\n";
    return 0;
}