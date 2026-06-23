// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mod/Seam_ModSignature.h"

// Inert (unoverridden) verifier default. With no real verifier registered the consumer falls back to
// hash-integrity only: any supplied manifest is reported HashOk so the manager's mount proceeds under
// its own structural guards (allowlist / sandbox / version / validation). A real implementer overrides
// VerifyManifest to validate signatures and return Signed, or Untrusted on tamper / denied signer.

FMod_TrustVerdict ISeam_ModSignatureVerifier::VerifyManifest_Implementation(const FMod_PackManifest& /*Manifest*/) const
{
	// Default: treat integrity hashes as the only evidence; allow the mount.
	return FMod_TrustVerdict(EMod_TrustLevel::HashOk);
}

FGameplayTag ISeam_ModSignatureVerifier::GetTrustPolicyId_Implementation() const
{
	// Default: no named trust policy.
	return FGameplayTag();
}
