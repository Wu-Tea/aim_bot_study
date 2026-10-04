#pragma once

#include "vision_service.h"
#include "vision_native/vision_engine.h"
#include <utility>

namespace runtime_app {

// Adapter shared by gamepad and mouse. The service owns this adapter, which
// exclusively owns the engine; engine calls stay on the service worker.
class VisionEngineServicePoller final : public IVisionServicePoller {
public:
    explicit VisionEngineServicePoller(std::unique_ptr<vision_native::VisionEngine> engine)
        : engine_(std::move(engine)) {}

    void set_request(pipeline_contract::VisionRequest request) override {
        engine_->set_request(request);
    }
    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) override {
        engine_->set_user_aim_intent(intent);
    }
    void set_viewport(const ViewportRequest& request) override {
        engine_->set_viewport(static_cast<int>(request.level), request.width,
            request.height, request.sequence, request.source_frame_id);
    }
    void set_detection_policy(const VisionDetectionPolicy& policy) override {
        engine_->set_detection_policy(policy.friendly, policy.height, policy.wide_height);
    }
    vision_native::VisionResult poll_once() override { return engine_->poll_once(); }

private:
    std::unique_ptr<vision_native::VisionEngine> engine_;
};

} // namespace runtime_app
