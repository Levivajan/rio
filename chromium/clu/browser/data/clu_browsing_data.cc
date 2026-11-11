// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/data/clu_browsing_data.h"

#include <memory>
#include <string>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "clu/browser/model/clu_model_service.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace clu {

namespace {

// Deletes itself once the remover reports back.
class RemoverJob : public content::BrowsingDataRemover::Observer {
 public:
  RemoverJob(content::BrowsingDataRemover* remover, base::OnceClosure done)
      : remover_(remover), done_(std::move(done)) {}
  RemoverJob(const RemoverJob&) = delete;
  RemoverJob& operator=(const RemoverJob&) = delete;
  ~RemoverJob() override = default;

  void Start(const ClearPlan& plan) {
    // The remover reports only to observers registered before the task starts.
    observation_.Observe(remover_);
    remover_->RemoveAndReply(plan.begin, plan.end, plan.remove_mask,
                             plan.origin_mask, this);
  }

  void StartFiltered(
      uint64_t remove_mask,
      std::unique_ptr<content::BrowsingDataFilterBuilder> filter) {
    observation_.Observe(remover_);
    remover_->RemoveWithFilterAndReply(
        base::Time(), base::Time::Max(), remove_mask,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
        std::move(filter), this);
  }

  // content::BrowsingDataRemover::Observer:
  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    if (failed_data_types) {
      LOG(WARNING) << "clu: clear browsing data failed for types "
                   << failed_data_types;
    }
    observation_.Reset();
    std::move(done_).Run();
    delete this;
  }

 private:
  const raw_ptr<content::BrowsingDataRemover> remover_;
  base::OnceClosure done_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      observation_{this};
};

}  // namespace

void ClearSiteData(Profile* profile,
                   const GURL& url,
                   uint64_t remove_mask,
                   base::OnceClosure done) {
  std::string site = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (site.empty()) {
    site = url.host();
  }
  if (!profile || site.empty() || !remove_mask) {
    std::move(done).Run();
    return;
  }
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter =
      content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kDelete);
  filter->AddRegisterableDomain(site);
  (new RemoverJob(profile->GetBrowsingDataRemover(), std::move(done)))
      ->StartFiltered(remove_mask, std::move(filter));
}

void RunClearPlan(Profile* profile,
                  const ClearPlan& plan,
                  base::OnceClosure done) {
  CluModelService* model = plan.archive ? CluModelService::Get(profile) : nullptr;
  const int parts = (plan.remove_mask ? 1 : 0) + (model ? 1 : 0);
  if (parts == 0) {
    std::move(done).Run();
    return;
  }
  base::RepeatingClosure barrier = base::BarrierClosure(parts, std::move(done));
  if (plan.remove_mask) {
    (new RemoverJob(profile->GetBrowsingDataRemover(), barrier))->Start(plan);
  }
  if (model) {
    model->ClearArchiveBetween(
        plan.archive_begin_ms, plan.archive_end_ms,
        base::BindOnce([](base::OnceClosure done, int) { std::move(done).Run(); },
                       barrier));
  }
}

}  // namespace clu
