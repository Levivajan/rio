// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_space_actions.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/widget/widget.h"

namespace clu {

namespace {

CluWindowFade::TickObserver& TickObserverForTesting() {
  static base::NoDestructor<CluWindowFade::TickObserver> observer;
  return *observer;
}

CluWindowFade::PaintGate& PaintGateForTesting() {
  static base::NoDestructor<CluWindowFade::PaintGate> gate;
  return *gate;
}

int& CompositorStepsForTesting() {
  static int steps = 0;
  return steps;
}

views::Widget* WidgetOf(CluWindowState* window) {
  ui::BaseWindow* base = window ? window->browser()->GetWindow() : nullptr;
  return base ? views::Widget::GetWidgetForNativeWindow(base->GetNativeWindow())
              : nullptr;
}

// Runs `callback` with the model of `profile_id` once its rows are read; null when the profile cannot load.
void WithModel(const std::string& profile_id,
               base::OnceCallback<void(CluModelService*)> callback) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces) {
    std::move(callback).Run(nullptr);
    return;
  }
  spaces->LoadProfile(
      profile_id,
      base::BindOnce(
          [](base::OnceCallback<void(CluModelService*)> callback,
             Profile* profile) {
            CluModelService* model =
                profile ? CluModelService::Get(profile) : nullptr;
            if (!model) {
              std::move(callback).Run(nullptr);
              return;
            }
            // Unretained: the model owns the callback it runs. The spaces service's own WhenLoaded hook ran first
            // (it was added when the factory built the model), so the model already knows its spaces.
            model->WhenLoaded(base::BindOnce(std::move(callback),
                                             base::Unretained(model)));
          },
          std::move(callback)));
}

int TabCount(CluModelService* model, const std::string& space_id) {
  if (!model) {
    return 0;
  }
  int count = static_cast<int>(model->tree().Today(space_id).size());
  for (const std::string& id : model->tree().Pinned(space_id)) {
    count += static_cast<int>(model->tree().TabsUnder(id).size());
  }
  return count;
}

void RunDone(base::OnceCallback<void(bool)> done, bool value) {
  if (done) {
    std::move(done).Run(value);
  }
}

}  // namespace

// CluWindowFade --------------------------------------------------------------------------------------------------------

// Steps the fade on every frame the target window's compositor begins, which viz paces from the display link of the
// screen the window is on (120 Hz on ProMotion, 60 on most externals), never on a 16 ms timer.
class CluWindowFade::FrameDriver : public ui::CompositorAnimationObserver {
 public:
  FrameDriver(CluWindowFade* fade, ui::Compositor* compositor) : fade_(fade) {
    observation_.Observe(compositor);
  }

  // ui::CompositorAnimationObserver. Step() may destroy this (the fade ends): nothing after it.
  void OnAnimationStep(base::TimeTicks timestamp) override {
    ++CompositorStepsForTesting();
    fade_->Step();
  }
  // The window is going: the fade's timer lands it.
  void OnCompositingShuttingDown(ui::Compositor* compositor) override {
    observation_.Reset();
  }

 private:
  const raw_ptr<CluWindowFade> fade_;
  base::ScopedObservation<ui::Compositor, ui::CompositorAnimationObserver>
      observation_{this};
};

CluWindowFade::CluWindowFade(base::WeakPtr<CluWindowState> from,
                             base::WeakPtr<CluWindowState> to,
                             bool close_source)
    : from_(std::move(from)), to_(std::move(to)), close_source_(close_source) {}

CluWindowFade::~CluWindowFade() {
  // A fade cut short lets the source settle again and leaves the target fully shown, never half transparent.
  if (from_ && !finished_) {
    from_->set_switch_pending(false);
  }
  if (to_) {
    if (views::Widget* widget = WidgetOf(to_.get())) {
      widget->SetOpacity(1.f);
    }
  }
}

// static
int CluWindowFade::compositor_steps_for_testing() {
  return CompositorStepsForTesting();
}

// static
void CluWindowFade::SetTickObserverForTesting(TickObserver observer) {
  TickObserverForTesting() = std::move(observer);
}

// static
void CluWindowFade::SetPaintGateForTesting(PaintGate gate) {
  PaintGateForTesting() = std::move(gate);
}

void CluWindowFade::Start() {
  if (!to_) {
    return;
  }
  ui::BaseWindow* target = to_->browser()->GetWindow();
  if (from_) {
    ui::BaseWindow* source = from_->browser()->GetWindow();
    const gfx::Rect bounds = source->GetBounds();
    if (!source->IsFullscreen() && !bounds.IsEmpty() &&
        target->GetBounds() != bounds) {
      target->SetBounds(bounds);
    }
  }
  if (views::Widget* widget = WidgetOf(to_.get())) {
    widget->SetOpacity(0.f);
  }
  to_->SetHiddenBySwitch(false);
  target->Show();
  paint_cap_.Start(FROM_HERE, kPaintCap,
                   base::BindOnce(&CluWindowFade::Finish,
                                  weak_ptr_factory_.GetWeakPtr()));
  to_->WhenFrameReady(base::BindOnce(&CluWindowFade::OnFrameReady,
                                     weak_ptr_factory_.GetWeakPtr()));
}

void CluWindowFade::OnFrameReady() {
  if (!to_ || finished_) {
    return;
  }
  // WhenFrameReady() has sent the frame the snapshot with the space. The rise waits until the frame's renderer has
  // put a frame on screen after that (the window shows, at opacity 0, so it composites), never on a fixed delay.
  content::WebContents* frame = to_->shell().frame_contents();
  if (!frame) {
    OnFramePainted();
    return;
  }
  frame->GetPrimaryMainFrame()->InsertVisualStateCallback(base::BindOnce(
      [](base::WeakPtr<CluWindowFade> fade, bool /*painted*/) {
        // A renderer that went away answers false; the rise goes on, and Finish() shows the window either way.
        if (fade) {
          fade->OnFramePainted();
        }
      },
      weak_ptr_factory_.GetWeakPtr()));
}

void CluWindowFade::OnFramePainted() {
  if (!to_ || finished_) {
    return;
  }
  if (PaintGateForTesting()) {
    PaintGateForTesting().Run(base::BindOnce(&CluWindowFade::Rise,
                                             weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  Rise();
}

void CluWindowFade::Rise() {
  paint_cap_.Stop();
  if (!to_ || finished_) {
    return;
  }
  if (gfx::Animation::PrefersReducedMotion()) {
    Finish();
    return;
  }
  start_ = base::TimeTicks::Now();
  if (views::Widget* widget = WidgetOf(to_.get())) {
    if (ui::Compositor* compositor = widget->GetCompositor()) {
      frame_driver_ = std::make_unique<FrameDriver>(this, compositor);
    }
  }
  timer_.Start(FROM_HERE, kDuration + kLandSlack, this, &CluWindowFade::Step);
  Step();
}

void CluWindowFade::Step() {
  if (!to_) {
    timer_.Stop();
    frame_driver_.reset();
    return;
  }
  const float opacity = std::clamp(
      static_cast<float>((base::TimeTicks::Now() - start_) / kDuration), 0.f,
      1.f);
  if (views::Widget* widget = WidgetOf(to_.get())) {
    widget->SetOpacity(opacity);
  }
  if (TickObserverForTesting()) {
    TickObserverForTesting().Run(
        opacity, from_ && from_->browser()->GetWindow()->IsVisible(),
        to_->frame_ready_for_testing());
  }
  if (opacity >= 1.f) {
    Finish();
  }
}

void CluWindowFade::Finish() {
  if (finished_) {
    return;
  }
  finished_ = true;
  timer_.Stop();
  frame_driver_.reset();
  paint_cap_.Stop();
  if (to_) {
    if (views::Widget* widget = WidgetOf(to_.get())) {
      widget->SetOpacity(1.f);
    }
  }
  if (from_ && from_.get() != to_.get()) {
    if (from_->FadeRunning()) {
      // The source became the target of a newer fade meanwhile: it stays on screen, and that fade owns it.
      from_->set_switch_pending(false);
    } else if (close_source_) {
      from_->browser()->GetWindow()->Close();
    } else {
      from_->SetHiddenBySwitch(true);
      from_->browser()->GetWindow()->Hide();
    }
  }
  if (TickObserverForTesting() && to_) {
    TickObserverForTesting().Run(1.f, false, to_->frame_ready_for_testing());
  }
  // The target owns this fade: it lets go from a posted task, after this call returns, unless a newer fade took its
  // place meanwhile.
  if (to_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<CluWindowState> to,
                          base::WeakPtr<CluWindowFade> fade) {
                         if (to && fade) {
                           to->ClearFade(fade.get());
                         }
                       },
                       to_, weak_ptr_factory_.GetWeakPtr()));
  }
}

namespace space_actions {

namespace {

// SwitchWindowToSpace(); `page_after_frame` for the switches the user asks for (CluWindowState::ShowSpace()).
void SwitchTo(CluWindowState* window,
              const std::string& space_id,
              bool close_source,
              base::OnceCallback<void(bool)> done,
              bool page_after_frame);

}  // namespace

std::vector<CluWindowState*> AllWindows() {
  std::vector<CluWindowState*> windows;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (CluWindowState* state =
            CluWindowState::Get(browser->GetUnownedUserDataHost())) {
      windows.push_back(state);
    }
  }
  return windows;
}

bool IsCrossProfile(CluWindowState* window, const DropTarget& target) {
  if (target.section == Section::kFavorite) {
    return false;
  }
  CluModelService* model = window->model();
  return model && !model->FindSpace(target.space_id);
}

void SwitchWindow(CluWindowState* window,
                  const SwitchTarget& target,
                  base::OnceCallback<void(bool)> done) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!window || window->is_private() || !window->model() || !spaces) {
    RunDone(std::move(done), false);
    return;
  }
  const std::optional<std::string> space_id =
      ResolveSwitch(spaces->list().Order(), window->space_id(), target);
  if (!space_id) {
    RunDone(std::move(done), false);
    return;
  }
  SwitchTo(window, *space_id, /*close_source=*/false, std::move(done),
           /*page_after_frame=*/true);
}

void SwitchWindowToSpace(CluWindowState* window,
                         const std::string& space_id,
                         bool close_source,
                         base::OnceCallback<void(bool)> done) {
  SwitchTo(window, space_id, close_source, std::move(done),
           /*page_after_frame=*/false);
}

namespace {

void SwitchTo(CluWindowState* window,
              const std::string& space_id,
              bool close_source,
              base::OnceCallback<void(bool)> done,
              bool page_after_frame) {
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* entry = spaces ? spaces->list().FindSpace(space_id)
                                   : nullptr;
  if (!window || !entry || window->is_private()) {
    RunDone(std::move(done), false);
    return;
  }
  // A window on its way out (its profile switch is loading, or it is the source of a running fade) takes no other
  // switch: two fades would each hide their source, and no window would be left on screen.
  if (window->switch_pending()) {
    RunDone(std::move(done), false);
    return;
  }
  // The window's own profile: in place.
  if (CluModelService* model = window->model();
      model && model->FindSpace(space_id)) {
    RunDone(std::move(done), window->ShowSpace(space_id, page_after_frame));
    return;
  }
  // A window still fading in (key repeat, a quick second press) settles first, then switches from there.
  window->FinishFade();
  window->set_switch_pending(true);
  spaces->LoadProfile(
      entry->profile_id,
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> from, std::string space_id,
             bool close_source, base::OnceCallback<void(bool)> done,
             Profile* profile) {
            if (!from) {
              RunDone(std::move(done), false);
              return;
            }
            // The source stays pending until the fade ends (CluWindowFade::Finish() hides it), so it never settles on
            // another space while it is still on screen under the fading target.
            if (!profile) {
              from->set_switch_pending(false);
              RunDone(std::move(done), false);
              return;
            }
            // The group's window of that profile, else a new one at the same bounds.
            CluWindowState* to = nullptr;
            for (CluWindowState* window : AllWindows()) {
              if (window != from.get() && window->profile() == profile &&
                  window->group_id() == from->group_id() &&
                  !window->browser()->IsDeleteScheduled()) {
                to = window;
                break;
              }
            }
            if (!to) {
              BrowserWindowCreateParams params(*profile,
                                               /*from_user_gesture=*/true);
              params.initial_bounds = from->browser()->GetWindow()->GetBounds();
              params.should_trigger_session_restore = false;
              BrowserWindowInterface* browser =
                  CreateBrowserWindow(std::move(params));
              to = browser ? CluWindowState::GetOrCreate(browser) : nullptr;
              if (!to) {
                from->set_switch_pending(false);
                RunDone(std::move(done), false);
                return;
              }
              to->SetGroup(from->group_id());
              to->PrepareEmpty();
            }
            to->SetInitialSpace(space_id);
            if (to->model()) {
              to->ShowSpace(space_id);
            }
            auto fade = std::make_unique<CluWindowFade>(
                from, to->GetWeakPtr(), close_source);
            CluWindowFade* raw = fade.get();
            to->SetFade(std::move(fade));
            raw->Start();
            RunDone(std::move(done), true);
          },
          window->GetWeakPtr(), space_id, close_source, std::move(done)));
}

}  // namespace

base::expected<std::string, SpacesError> CreateSpace(
    CluWindowState* window,
    const std::optional<std::string>& name,
    const std::optional<SpaceIconInput>& icon,
    const std::optional<std::string>& color,
    const std::optional<std::string>& profile_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces) {
    return base::unexpected(SpacesError::kNotFound);
  }
  base::expected<std::string, SpacesError> id =
      spaces->CreateSpace(name, icon, color, profile_id);
  if (id.has_value() && window) {
    // The new space goes right after the window's space.
    const int current = spaces->list().IndexOf(window->space_id());
    if (current >= 0) {
      std::ignore = spaces->ReorderSpace(id.value(), current + 1);
    }
    SwitchWindowToSpace(window, id.value(), /*close_source=*/false);
  }
  return id;
}

void DeleteSpace(
    const std::string& space_id,
    base::OnceCallback<void(base::expected<void, SpacesError>)> done) {
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* entry = spaces ? spaces->list().FindSpace(space_id)
                                   : nullptr;
  if (!entry) {
    std::move(done).Run(base::unexpected(SpacesError::kNotFound));
    return;
  }
  if (spaces->list().spaces().size() <= 1) {
    std::move(done).Run(base::unexpected(SpacesError::kLastSpace));
    return;
  }
  WithModel(
      entry->profile_id,
      base::BindOnce(
          [](std::string space_id,
             base::OnceCallback<void(base::expected<void, SpacesError>)> done,
             CluModelService* model) {
            CluSpacesService* spaces = CluSpacesService::Get();
            if (!spaces || !spaces->list().FindSpace(space_id)) {
              std::move(done).Run(base::unexpected(SpacesError::kNotFound));
              return;
            }
            const std::optional<std::string> fallback =
                FallbackSpace(spaces->list().Order(), space_id);
            if (!fallback) {
              std::move(done).Run(base::unexpected(SpacesError::kLastSpace));
              return;
            }
            // Windows leave first, so the archive's fallback never picks a row of the space that goes.
            for (CluWindowState* window : AllWindows()) {
              // Hidden siblings settle by themselves once the space is gone (ValidateSpace()).
              if (window->space_id() == space_id && !window->is_private() &&
                  !window->hidden_by_switch()) {
                SwitchWindowToSpace(window, *fallback, /*close_source=*/false);
              }
            }
            if (model) {
              model->DeleteSpaceRows(space_id);
            }
            base::expected<std::string, SpacesError> deleted =
                spaces->DeleteSpace(space_id);
            if (!deleted.has_value()) {
              std::move(done).Run(base::unexpected(deleted.error()));
              return;
            }
            std::move(done).Run(base::ok());
          },
          space_id, std::move(done)));
}

void ConfirmDeleteSpace(CluWindowState* window, const std::string& space_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* entry = spaces ? spaces->list().FindSpace(space_id)
                                   : nullptr;
  if (!window || !entry || spaces->list().spaces().size() <= 1) {
    return;
  }
  WithModel(
      entry->profile_id,
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> window, std::string space_id,
             CluModelService* model) {
            CluSpacesService* spaces = CluSpacesService::Get();
            const SpaceEntry* entry =
                spaces ? spaces->list().FindSpace(space_id) : nullptr;
            if (!window || !entry) {
              return;
            }
            int pinned = 0;
            int today = 0;
            if (model) {
              today = static_cast<int>(model->tree().Today(space_id).size());
              pinned = TabCount(model, space_id) - today;
            }
            const DeleteSpaceText text =
                MakeDeleteSpaceText(entry->name, pinned, today);
            window->OpenConfirm(
                base::DictValue()
                    .Set("title", text.title)
                    .Set("body", text.body)
                    .Set("confirmLabel", "Delete")
                    .Set("cancelLabel", "Cancel")
                    .Set("destructive", true),
                base::BindOnce(
                    [](base::WeakPtr<CluWindowState> window,
                       std::string space_id, bool confirmed) {
                      if (!confirmed) {
                        return;
                      }
                      DeleteSpace(
                          space_id,
                          base::BindOnce(
                              [](base::WeakPtr<CluWindowState> window,
                                 base::expected<void, SpacesError> result) {
                                if (!result.has_value() && window) {
                                  window->ShowToast(
                                      "The space could not be deleted",
                                      std::nullopt, base::Milliseconds(1500));
                                }
                              },
                              window));
                    },
                    window, space_id));
          },
          window->GetWeakPtr(), space_id));
}

void SetSpaceProfile(
    const std::string& space_id,
    const std::string& profile_id,
    bool confirmed,
    base::OnceCallback<void(base::expected<SetProfileResult, SpacesError>)>
        done) {
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* entry = spaces ? spaces->list().FindSpace(space_id)
                                   : nullptr;
  if (!entry || !spaces->list().FindProfile(profile_id)) {
    std::move(done).Run(base::unexpected(SpacesError::kNotFound));
    return;
  }
  if (entry->profile_id == profile_id) {
    std::move(done).Run(SetProfileResult());
    return;
  }
  WithModel(
      entry->profile_id,
      base::BindOnce(
          [](std::string space_id, std::string profile_id, bool confirmed,
             base::OnceCallback<void(
                 base::expected<SetProfileResult, SpacesError>)> done,
             CluModelService* source) {
            const int tab_count = TabCount(source, space_id);
            if (tab_count > 0 && !confirmed) {
              std::move(done).Run(SetProfileResult{.needs_confirmation = true,
                                                   .tab_count = tab_count});
              return;
            }
            WithModel(
                profile_id,
                base::BindOnce(
                    [](std::string space_id, std::string profile_id,
                       base::WeakPtr<CluModelService> source,
                       base::OnceCallback<void(
                           base::expected<SetProfileResult, SpacesError>)>
                           done,
                       CluModelService* target) {
                      CluSpacesService* spaces = CluSpacesService::Get();
                      const SpaceEntry* entry =
                          spaces ? spaces->list().FindSpace(space_id)
                                 : nullptr;
                      if (!entry || !target) {
                        std::move(done).Run(
                            base::unexpected(SpacesError::kNotFound));
                        return;
                      }
                      // Windows that show the space follow it into the other profile.
                      std::vector<base::WeakPtr<CluWindowState>> showing;
                      for (CluWindowState* window : AllWindows()) {
                        if (window->space_id() == space_id &&
                            !window->is_private() &&
                            !window->hidden_by_switch()) {
                          window->set_switch_pending(true);
                          showing.push_back(window->GetWeakPtr());
                        }
                      }
                      CluModelService::MovedRows rows;
                      if (source) {
                        rows = source->TakeSpaceRows(space_id);
                      }
                      base::expected<void, SpacesError> moved =
                          spaces->SetSpaceProfile(space_id, profile_id);
                      if (!moved.has_value()) {
                        // The rows go back where they were, with their history.
                        if (source) {
                          source->ReturnSpaceRows(space_id, std::move(rows));
                        }
                        for (const auto& window : showing) {
                          if (window) {
                            window->set_switch_pending(false);
                          }
                        }
                        std::move(done).Run(base::unexpected(moved.error()));
                        return;
                      }
                      target->AddSpaceRows(space_id, std::move(rows));
                      for (const auto& window : showing) {
                        if (window) {
                          window->set_switch_pending(false);
                          SwitchWindowToSpace(window.get(), space_id,
                                              /*close_source=*/false);
                        }
                      }
                      std::move(done).Run(SetProfileResult());
                    },
                    space_id, profile_id,
                    source ? source->GetWeakPtr()
                           : base::WeakPtr<CluModelService>(),
                    std::move(done)));
          },
          space_id, profile_id, confirmed, std::move(done)));
}

void MoveRows(
    CluWindowState* window,
    std::vector<std::string> ids,
    DropTarget target,
    bool confirmed,
    bool ask,
    base::OnceCallback<void(base::expected<MoveStatus, TreeError>)> done) {
  CluModelService* model = window ? window->model() : nullptr;
  if (!model || ids.empty()) {
    std::move(done).Run(base::unexpected(TreeError::kNotFound));
    return;
  }
  if (!IsCrossProfile(window, target)) {
    base::expected<void, TreeError> moved =
        window->MoveRows(ids, target, /*toast=*/ask);
    if (!moved.has_value()) {
      std::move(done).Run(base::unexpected(moved.error()));
      return;
    }
    std::move(done).Run(MoveStatus::kMoved);
    return;
  }
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* entry = spaces ? spaces->list().FindSpace(target.space_id)
                                   : nullptr;
  const ProfileEntry* profile =
      entry ? spaces->list().FindProfile(entry->profile_id) : nullptr;
  if (!entry || !profile) {
    std::move(done).Run(base::unexpected(TreeError::kBadTarget));
    return;
  }
  for (const std::string& id : ids) {
    if (!model->tree().Get(id)) {
      std::move(done).Run(base::unexpected(TreeError::kNotFound));
      return;
    }
  }
  const std::string profile_name = profile->name;
  auto run = base::BindOnce(
      [](base::WeakPtr<CluWindowState> window, std::vector<std::string> ids,
         DropTarget target, std::string profile_id, std::string profile_name,
         base::OnceCallback<void(base::expected<MoveStatus, TreeError>)> done) {
        if (!window || !window->model()) {
          std::move(done).Run(base::unexpected(TreeError::kNotFound));
          return;
        }
        base::WeakPtr<CluModelService> source = window->model()->GetWeakPtr();
        WithModel(
            profile_id,
            base::BindOnce(
                [](base::WeakPtr<CluWindowState> window,
                   base::WeakPtr<CluModelService> source,
                   std::vector<std::string> ids, DropTarget target,
                   std::string profile_name,
                   base::OnceCallback<void(
                       base::expected<MoveStatus, TreeError>)> done,
                   CluModelService* destination) {
                  if (!source || !destination) {
                    std::move(done).Run(
                        base::unexpected(TreeError::kBadTarget));
                    return;
                  }
                  bool any = false;
                  std::optional<TreeError> refused;
                  for (const std::string& id : ids) {
                    const CluModelService::MovedRows preview =
                        source->CopySubtree(id);
                    base::expected<void, TreeError> allowed =
                        destination->CanAddSubtree(preview, target);
                    if (!allowed.has_value()) {
                      refused = allowed.error();
                      continue;
                    }
                    // Windows showing a moving row show their next row first.
                    for (CluWindowState* other : AllWindows()) {
                      other->LeaveRow(id);
                    }
                    std::ignore = destination->AddSubtree(
                        source->TakeSubtree(id), target);
                    ++target.index;
                    any = true;
                  }
                  if (!any) {
                    std::move(done).Run(base::unexpected(
                        refused.value_or(TreeError::kNotFound)));
                    return;
                  }
                  if (window) {
                    window->ShowToast(
                        base::StrCat({"Reloaded in ", profile_name}),
                        std::nullopt, base::Milliseconds(1500));
                  }
                  std::move(done).Run(MoveStatus::kMoved);
                },
                window, source, std::move(ids), std::move(target),
                std::move(profile_name), std::move(done)));
      },
      window->GetWeakPtr(), std::move(ids), target, entry->profile_id,
      profile_name);
  if (confirmed) {
    std::move(run).Run(std::move(done));
    return;
  }
  if (!ask) {
    std::move(done).Run(MoveStatus::kNeedsConfirmation);
    return;
  }
  // F11: moving into a space of another profile warns first.
  window->OpenConfirm(
      base::DictValue()
          .Set("title", base::StrCat({"Move to ", profile_name, "?"}))
          .Set("body",
               base::StrCat({"The page reloads in ", profile_name,
                             ", and its back and forward history is cleared. "
                             "You may need to sign in again."}))
          .Set("confirmLabel", "Move")
          .Set("cancelLabel", "Cancel"),
      base::BindOnce(
          [](decltype(run) run,
             base::OnceCallback<void(base::expected<MoveStatus, TreeError>)>
                 done,
             bool confirmed) {
            if (!confirmed) {
              std::move(done).Run(MoveStatus::kNeedsConfirmation);
              return;
            }
            std::move(run).Run(std::move(done));
          },
          std::move(run), std::move(done)));
}

void OpenPrivateWindow(Profile* profile) {
  if (!profile) {
    return;
  }
  chrome::NewIncognitoWindow(profile->GetOriginalProfile());
}

}  // namespace space_actions

}  // namespace clu
