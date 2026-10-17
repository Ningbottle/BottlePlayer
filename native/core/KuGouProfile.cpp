#include "echo/core/KuGouProfile.h"

namespace echo::core {

KuGouProfileParams GetKuGouProfile(KuGouEdition edition) {
  switch (edition) {
    case KuGouEdition::Concept:
      return KuGouProfileParams{
          .appid = "3116",
          .clientver = "11440",
          .busiType = "concept",
          .saltKind = KuGouSaltKind::Lite,
      };
    case KuGouEdition::Standard:
      return KuGouProfileParams{
          .appid = "1005",
          .clientver = "20489",
          .busiType = "",
          .saltKind = KuGouSaltKind::Standard,
      };
  }
  // Unreachable — keep compiler happy.
  return GetKuGouProfile(KuGouEdition::Concept);
}

const char* KuGouSaltKindName(KuGouSaltKind kind) {
  return kind == KuGouSaltKind::Standard ? "standard" : "lite";
}

std::string KuGouProfileName(const KuGouProfileParams& profile) {
  for (const KuGouEdition edition : {KuGouEdition::Standard, KuGouEdition::Concept}) {
    const auto preset = GetKuGouProfile(edition);
    if (preset.appid == profile.appid && preset.clientver == profile.clientver &&
        preset.saltKind == profile.saltKind) {
      return edition == KuGouEdition::Standard ? "standard" : "concept";
    }
  }
  return "custom";
}

ConceptUrlParams GetConceptUrlParams() {
  return ConceptUrlParams{
      .pageId = "967177915",
      .pid = "411",
      .ppageId = "356753938,823673182,967485191",
  };
}

}  // namespace echo::core
