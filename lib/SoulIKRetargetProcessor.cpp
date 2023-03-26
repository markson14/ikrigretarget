//
//  IKRetargetProcessor.cpp
//  
//
//  Created by kai chen on 3/24/23.
//

#include "SoulIKRetargetProcessor.h"
#include <algorithm>
#include <tuple>


#define RETARGETSKELETON_INVALID_BRANCH_INDEX -2

using namespace Soul;

void FRetargetSkeleton::Initialize(
	USkeleton* InSkeleton,
	const TArray<FBoneChain>& BoneChains,
	const FName InRetargetPoseName,
	const FIKRetargetPose* RetargetPose,
	const FName RetargetRootBone)
{
	// reset all skeleton data
	Reset();
	
	// record which skeletal mesh this is running on
	Skeleton = InSkeleton;
	
	// copy names and parent indices into local storage
	const USkeleton* RefSkeleton = Skeleton;
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton->GetNum(); ++BoneIndex)
	{
		BoneNames.push_back(RefSkeleton->GetBoneName(BoneIndex));
		ParentIndices.push_back(RefSkeleton->GetParentIndex(BoneIndex));	
	}

	// determine set of bones referenced by one of the retarget bone chains
	// this is the set of bones that will be affected by the retarget pose
	ChainThatContainsBone.resize(BoneNames.size(), NAME_None);
	for (const FBoneChain& BoneChain : BoneChains)
	{
		TArray<int32> BonesInChain;
		if (FResolvedBoneChain(BoneChain, *this, BonesInChain).IsValid())
		{
			for (const int32 BoneInChain : BonesInChain)
			{
				ChainThatContainsBone[BoneInChain] = BoneChain.ChainName;
			}
		}
	}

	// initialize branch caching
	//CachedEndOfBranchIndices.Init(RETARGETSKELETON_INVALID_BRANCH_INDEX, ParentIndices.Num());

	// update retarget pose to reflect custom offsets (applies stored offsets)
	// NOTE: this must be done AFTER generating IsBoneInAnyTargetChain array above
	GenerateRetargetPose(InRetargetPoseName, RetargetPose, RetargetRootBone);
}

void FRetargetSkeleton::Reset()
{
	BoneNames.clear();
	ParentIndices.clear();
	RetargetLocalPose.clear();
	RetargetGlobalPose.clear();
	Skeleton = nullptr;
}

void FRetargetSkeleton::GenerateRetargetPose(
	const FName InRetargetPoseName,
	const FIKRetargetPose* InRetargetPose,
	const FName RetargetRootBone)
{
	// record the name of the retarget pose (prevents re-initialization if profile swaps it)
	RetargetPoseName = InRetargetPoseName;
	
	// initialize retarget pose to the skeletal mesh reference pose
	RetargetLocalPose = Skeleton->GetRefBonePose();
	// copy local pose to global
	RetargetGlobalPose = RetargetLocalPose;
	// convert to global space
	UpdateGlobalTransformsBelowBone(-1, RetargetLocalPose, RetargetGlobalPose);

	// no retarget pose specified (will use default pose from skeletal mesh with no offsets)
	if (InRetargetPose==nullptr  || RetargetRootBone == NAME_None)
	{
		return;
	}

	// apply retarget pose offsets (retarget pose is stored as offset relative to reference pose)
	const TArray<FTransform>& RefPoseLocal = Skeleton->GetRefBonePose();
	
	// apply root translation offset
	const int32 RootBoneIndex = FindBoneIndexByName(RetargetRootBone);
	if (RootBoneIndex != INDEX_NONE)
	{
		FTransform& RootTransform = RetargetGlobalPose[RootBoneIndex];
		RootTransform.AddToTranslation(InRetargetPose->GetRootTranslationDelta());
		UpdateLocalTransformOfSingleBone(RootBoneIndex, RetargetLocalPose, RetargetGlobalPose);
	}

	// apply bone rotation offsets
	for (const auto& BoneDelta : InRetargetPose->GetAllDeltaRotations())
	{
		const int32 BoneIndex = FindBoneIndexByName(BoneDelta.first);
		if (BoneIndex == INDEX_NONE)
		{
			// this can happen if a retarget pose recorded a bone offset for a bone that is not present in the
			// target skeleton; ie, the retarget pose was generated from a different Skeletal Mesh with extra bones
			continue;
		}

		const FQuat LocalBoneRotation = RefPoseLocal[BoneIndex].GetRotation() * BoneDelta.second;
		RetargetLocalPose[BoneIndex].SetRotation(LocalBoneRotation);
	}

	UpdateGlobalTransformsBelowBone(-1, RetargetLocalPose, RetargetGlobalPose);
}

int32 FRetargetSkeleton::FindBoneIndexByName(const FName InName) const
{
	auto it = std::find(BoneNames.begin(), BoneNames.end(), InName);
	if (it == BoneNames.end()) {
		return -1;
	} else {
		return std::distance(BoneNames.begin(), it);
	}
}

void FRetargetSkeleton::UpdateGlobalTransformsBelowBone(
	const int32 StartBoneIndex,
	const TArray<FTransform>& InLocalPose,
	TArray<FTransform>& OutGlobalPose) const
{
	//check(BoneNames.IsValidIndex(StartBoneIndex+1));
	//check(BoneNames.Num() == InLocalPose.Num());
	//check(BoneNames.Num() == OutGlobalPose.Num());
	
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<OutGlobalPose.size(); ++BoneIndex)
	{
		UpdateGlobalTransformOfSingleBone(BoneIndex,InLocalPose,OutGlobalPose);
	}
}

void FRetargetSkeleton::UpdateLocalTransformsBelowBone(
	const int32 StartBoneIndex,
	TArray<FTransform>& OutLocalPose,
	const TArray<FTransform>& InGlobalPose) const
{
	//check(BoneNames.IsValidIndex(StartBoneIndex));
	//check(BoneNames.Num() == OutLocalPose.Num());
	//check(BoneNames.Num() == InGlobalPose.Num());
	
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<InGlobalPose.size(); ++BoneIndex)
	{
		UpdateLocalTransformOfSingleBone(BoneIndex, OutLocalPose, InGlobalPose);
	}
}

void FRetargetSkeleton::UpdateGlobalTransformOfSingleBone(
	const int32 BoneIndex,
	const TArray<FTransform>& InLocalPose,
	TArray<FTransform>& OutGlobalPose) const
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		// root always in global space already, no conversion required
		OutGlobalPose[BoneIndex] = InLocalPose[BoneIndex];
		return; 
	}
	const FTransform& ChildLocalTransform = InLocalPose[BoneIndex];
	const FTransform& ParentGlobalTransform = OutGlobalPose[ParentIndex];
	OutGlobalPose[BoneIndex] = ChildLocalTransform * ParentGlobalTransform;
}

void FRetargetSkeleton::UpdateLocalTransformOfSingleBone(
	const int32 BoneIndex,
	TArray<FTransform>& OutLocalPose,
	const TArray<FTransform>& InGlobalPose) const
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		// root bone, so just set the local pose to the global pose
		OutLocalPose[BoneIndex] = InGlobalPose[BoneIndex];
		return;
	}
	const FTransform& ChildGlobalTransform = InGlobalPose[BoneIndex];
	const FTransform& ParentGlobalTransform = InGlobalPose[ParentIndex];
	OutLocalPose[BoneIndex] = ChildGlobalTransform.GetRelativeTransform(ParentGlobalTransform);
}

FTransform FRetargetSkeleton::GetGlobalRefPoseOfSingleBone(
	const int32 BoneIndex,
	const TArray<FTransform>& InGlobalPose) const
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		return RetargetLocalPose[BoneIndex]; // root always in global space
	}
	const FTransform& ChildLocalTransform = RetargetLocalPose[BoneIndex];
	const FTransform& ParentGlobalTransform = InGlobalPose[ParentIndex];
	return ChildLocalTransform * ParentGlobalTransform;
}

int32 FRetargetSkeleton::GetCachedEndOfBranchIndex(const int32 InBoneIndex) const
{
	if ( InBoneIndex >= CachedEndOfBranchIndices.size())
	{
		return INDEX_NONE;
	}

	// already cached
	if (CachedEndOfBranchIndices[InBoneIndex] != RETARGETSKELETON_INVALID_BRANCH_INDEX)
	{
		return CachedEndOfBranchIndices[InBoneIndex];
	}

	const int32 NumBones = BoneNames.size();
	
	// if we're asking for root's branch, get the last bone  
	if (InBoneIndex == 0)
	{
		CachedEndOfBranchIndices[InBoneIndex] = NumBones-1;
		return CachedEndOfBranchIndices[InBoneIndex];
	}

	CachedEndOfBranchIndices[InBoneIndex] = INDEX_NONE;
	const int32 StartParentIndex = GetParentIndex(InBoneIndex);
	int32 BoneIndex = InBoneIndex + 1;
	int32 ParentIndex = GetParentIndex(BoneIndex);

	// if next child bone's parent is less than or equal to StartParentIndex,
	// we are leaving the branch so no need to go further
	while (ParentIndex > StartParentIndex && BoneIndex < NumBones)
	{
		CachedEndOfBranchIndices[InBoneIndex] = BoneIndex;
				
		BoneIndex++;
		ParentIndex = GetParentIndex(BoneIndex);
	}

	return CachedEndOfBranchIndices[InBoneIndex];
}

void FRetargetSkeleton::GetChildrenIndices(const int32 BoneIndex, TArray<int32>& OutChildren) const
{
	const int32 LastBranchIndex = GetCachedEndOfBranchIndex(BoneIndex);
	if (LastBranchIndex == INDEX_NONE)
	{
		// no children (leaf bone)
		return;
	}
	
	for (int32 ChildBoneIndex = BoneIndex + 1; ChildBoneIndex <= LastBranchIndex; ChildBoneIndex++)
	{
		if (GetParentIndex(ChildBoneIndex) == BoneIndex)
		{
			OutChildren.push_back(ChildBoneIndex);
		}
	}
}

void FRetargetSkeleton::GetChildrenIndicesRecursive(const int32 BoneIndex, TArray<int32>& OutChildren) const
{
	const int32 LastBranchIndex = GetCachedEndOfBranchIndex(BoneIndex);
	if (LastBranchIndex == INDEX_NONE)
	{
		// no children (leaf bone)
		return;
	}
	
	for (int32 ChildBoneIndex = BoneIndex + 1; ChildBoneIndex <= LastBranchIndex; ChildBoneIndex++)
	{
		OutChildren.push_back(ChildBoneIndex);
	}
}

bool FRetargetSkeleton::IsParentOfChild(const int32 PotentialParentIndex, const int32 ChildBoneIndex) const
{
	int32 ParentIndex = GetParentIndex(ChildBoneIndex);
	while (ParentIndex != INDEX_NONE)
	{
		if (ParentIndex == PotentialParentIndex)
		{
			return true;
		}
		
		ParentIndex = GetParentIndex(ParentIndex);
	}
	
	return false;
}

int32 FRetargetSkeleton::GetParentIndex(const int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex>=ParentIndices.size() || BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return ParentIndices[BoneIndex];
}

void FTargetSkeleton::Initialize(
	USkeleton* InSkeleton,
	const TArray<FBoneChain>& BoneChains,
	const FName InRetargetPoseName,
	const FIKRetargetPose* RetargetPose,
	const FName RetargetRootBone)
{
	Reset();
	
	FRetargetSkeleton::Initialize(InSkeleton, BoneChains, InRetargetPoseName, RetargetPose, RetargetRootBone);

	// make storage for per-bone "Is Retargeted" flag (used for hierarchy updates)
	// these are bones that are in a target chain that is mapped to a source chain (ie, will actually be retargeted)
	// these flags are actually set later in init phase when bone chains are mapped together
	IsBoneRetargeted.resize(BoneNames.size(), false);

	// initialize storage for output pose (the result of the retargeting)
	OutputGlobalPose = RetargetGlobalPose;
}

void FTargetSkeleton::Reset()
{
	FRetargetSkeleton::Reset();
	OutputGlobalPose.clear();
	IsBoneRetargeted.clear();
}

void FTargetSkeleton::UpdateGlobalTransformsAllNonRetargetedBones(TArray<FTransform>& InOutGlobalPose)
{
	//check(IsBoneRetargeted.Num() == InOutGlobalPose.Num());
	
	for (int32 BoneIndex=0; BoneIndex<InOutGlobalPose.size(); ++BoneIndex)
	{
		if (!IsBoneRetargeted[BoneIndex])
		{
			UpdateGlobalTransformOfSingleBone(BoneIndex, RetargetLocalPose, InOutGlobalPose);
		}
	}
}

FResolvedBoneChain::FResolvedBoneChain(
	const FBoneChain& BoneChain,
	const FRetargetSkeleton& Skeleton,
	TArray<int32>& OutBoneIndices)
{
	// validate start and end bones exist and are not the root
	const int32 StartIndex = Skeleton.FindBoneIndexByName(BoneChain.StartBone.BoneName);
	const int32 EndIndex = Skeleton.FindBoneIndexByName(BoneChain.EndBone.BoneName);
	bFoundStartBone = StartIndex > INDEX_NONE;
	bFoundEndBone = EndIndex > INDEX_NONE;

	// no need to build the chain if start/end indices are wrong 
	const bool bIsWellFormed = bFoundStartBone && bFoundEndBone && EndIndex >= StartIndex;
	if (bIsWellFormed)
	{
		// init array with end bone 
		OutBoneIndices = {EndIndex};

		// if only one bone in the chain
		if (EndIndex == StartIndex)
		{
			// validate end bone is child of start bone ?
			bEndIsStartOrChildOfStart = true;
			return;
		}

		// record all bones in chain while walking up the hierarchy (tip to root of chain)
		int32 ParentIndex = Skeleton.GetParentIndex(EndIndex);
		while (ParentIndex > INDEX_NONE && ParentIndex >= StartIndex)
		{
			OutBoneIndices.push_back(ParentIndex);
			ParentIndex = Skeleton.GetParentIndex(ParentIndex);
		}

		// if we walked up till the start bone
		if (OutBoneIndices.back() == StartIndex)
		{
			// validate end bone is child of start bone
			bEndIsStartOrChildOfStart = true;
			// reverse the indices (we want root to tip order)
			std::reverse(OutBoneIndices.begin(), OutBoneIndices.end());
			return;
		}
      
		// oops, we walked all the way up without finding the start bone
		OutBoneIndices.clear();
	}
}

void FTargetSkeleton::SetBoneIsRetargeted(const int32 BoneIndex, const bool IsRetargeted)
{
	//check(IsBoneRetargeted.IsValidIndex(BoneIndex));
	IsBoneRetargeted[BoneIndex] = IsRetargeted;
}

bool FChainFK::Initialize(
	const FRetargetSkeleton& Skeleton,
	const TArray<int32>& InBoneIndices,
	const TArray<FTransform>& InitialGlobalPose,
	FIKRigLogger& Log)
{
	//check(!InBoneIndices.IsEmpty());

	// store for debugging purposes
	BoneIndices = InBoneIndices;

	// store all the initial bone transforms in the bone chain
	InitialGlobalTransforms.clear();
	for (int32 Index=0; Index < BoneIndices.size(); ++Index)
	{
		const int32 BoneIndex = BoneIndices[Index];
		if (BoneIndex < InitialGlobalPose.size())
		{
			InitialGlobalTransforms.emplace_back(InitialGlobalPose[BoneIndex]);
		}
	}

	// initialize storage for current bones
	CurrentGlobalTransforms = InitialGlobalTransforms;

	// get the local space of the chain in retarget pose
	InitialLocalTransforms.resize(InitialGlobalTransforms.size());
	FillTransformsWithLocalSpaceOfChain(Skeleton, InitialGlobalPose, BoneIndices, InitialLocalTransforms);

	// store chain parent data
	ChainParentBoneIndex = Skeleton.GetParentIndex(BoneIndices[0]);
	ChainParentInitialGlobalTransform = FTransform::Identity;
	if (ChainParentBoneIndex != INDEX_NONE)
	{
		ChainParentInitialGlobalTransform = InitialGlobalPose[ChainParentBoneIndex];
	}

	// calculate parameter of each bone, normalized by the length of the bone chain
	return CalculateBoneParameters(Log);
}

bool FChainFK::CalculateBoneParameters(FIKRigLogger& Log)
{
	Params.clear();
	
	// special case, a single-bone chain
	if (InitialGlobalTransforms.size() == 1)
	{
		Params.push_back(1.0f);
		return true;
	}

	// calculate bone lengths in chain and accumulate total length
	TArray<float> BoneDistances;
	float TotalChainLength = 0.0f;
	BoneDistances.push_back(0.0f);
	for (int32 i=1; i<InitialGlobalTransforms.size(); ++i)
	{
		TotalChainLength += (InitialGlobalTransforms[i].GetTranslation() - InitialGlobalTransforms[i-1].GetTranslation()).Size();
		BoneDistances.push_back(TotalChainLength);
	}

	// cannot retarget chain if all the bones are sitting directly on each other
	if (TotalChainLength <= KINDA_SMALL_NUMBER)
	{
		Log.LogWarning("TinyBoneChain, IK Retargeter bone chain length is too small to reliably retarget.");
		return false;
	}

	// calc each bone's param along length
	for (int32 i=0; i<InitialGlobalTransforms.size(); ++i)
	{
		Params.push_back(BoneDistances[i] / TotalChainLength); 
	}

	return true;
}

void FChainFK::FillTransformsWithLocalSpaceOfChain(
	const FRetargetSkeleton& Skeleton,
	const TArray<FTransform>& InGlobalPose,
	const TArray<int32>& BoneIndices,
	TArray<FTransform>& OutLocalTransforms)
{
	//check(BoneIndices.Num() == OutLocalTransforms.Num())
	
	for (int32 ChainIndex=0; ChainIndex<BoneIndices.size(); ++ChainIndex)
	{
		const int32 BoneIndex = BoneIndices[ChainIndex];
		const int32 ParentIndex = Skeleton.GetParentIndex(BoneIndex);
		if (ParentIndex == INDEX_NONE)
		{
			// root is always in "global" space
			OutLocalTransforms[ChainIndex] = InGlobalPose[BoneIndex];
			continue;
		}

		const FTransform& ChildGlobalTransform = InGlobalPose[BoneIndex];
		const FTransform& ParentGlobalTransform = InGlobalPose[ParentIndex];
		OutLocalTransforms[ChainIndex] = ChildGlobalTransform.GetRelativeTransform(ParentGlobalTransform);
	}
}

void FChainFK::PutCurrentTransformsInRefPose(
	const TArray<int32>& InBoneIndices,
	const FRetargetSkeleton& Skeleton,
	const TArray<FTransform>& InCurrentGlobalPose)
{
	// update chain current transforms to the retarget pose in global space
	for (int32 ChainIndex=0; ChainIndex<InBoneIndices.size(); ++ChainIndex)
	{
		// update first bone in chain based on the incoming parent
		if (ChainIndex == 0)
		{
			const int32 BoneIndex = InBoneIndices[ChainIndex];
			CurrentGlobalTransforms[ChainIndex] = Skeleton.GetGlobalRefPoseOfSingleBone(BoneIndex, InCurrentGlobalPose);
		}
		else
		{
			// all subsequent bones in chain are based on previous parent
			const int32 BoneIndex = InBoneIndices[ChainIndex];
			const FTransform& ParentGlobalTransform = CurrentGlobalTransforms[ChainIndex-1];
			const FTransform& ChildLocalTransform = Skeleton.RetargetLocalPose[BoneIndex];
			CurrentGlobalTransforms[ChainIndex] = ChildLocalTransform * ParentGlobalTransform;
		}
	}
}

void FChainEncoderFK::EncodePose(
	const FRetargetSkeleton& SourceSkeleton,
	const TArray<int32>& SourceBoneIndices,
    const TArray<FTransform> &InSourceGlobalPose)
{
	//check(SourceBoneIndices.Num() == CurrentGlobalTransforms.Num());
	
	// copy the global input pose for the chain
	for (int32 ChainIndex=0; ChainIndex<SourceBoneIndices.size(); ++ChainIndex)
	{
		const int32 BoneIndex = SourceBoneIndices[ChainIndex];
		CurrentGlobalTransforms[ChainIndex] = InSourceGlobalPose[BoneIndex];
	}

	CurrentLocalTransforms.resize(SourceBoneIndices.size());
	FillTransformsWithLocalSpaceOfChain(SourceSkeleton, InSourceGlobalPose, SourceBoneIndices, CurrentLocalTransforms);

	if (ChainParentBoneIndex != INDEX_NONE)
	{
		ChainParentCurrentGlobalTransform = InSourceGlobalPose[ChainParentBoneIndex];
	}
}

void FChainEncoderFK::TransformCurrentChainTransforms(const FTransform& NewParentTransform)
{
	for (int32 ChainIndex=0; ChainIndex<CurrentGlobalTransforms.size(); ++ChainIndex)
	{
		if (ChainIndex == 0)
		{
			CurrentGlobalTransforms[ChainIndex] = CurrentLocalTransforms[ChainIndex] * NewParentTransform;
		}
		else
		{
			CurrentGlobalTransforms[ChainIndex] = CurrentLocalTransforms[ChainIndex] * CurrentGlobalTransforms[ChainIndex-1];
		}
	}
}

void FChainDecoderFK::DecodePose(
	const FRootRetargeter& RootRetargeter,
	const FTargetChainSettings& Settings,
	const TArray<int32>& TargetBoneIndices,
    FChainEncoderFK& SourceChain,
    const FTargetSkeleton& TargetSkeleton,
    TArray<FTransform> &InOutGlobalPose)
{
	//check(TargetBoneIndices.Num() == CurrentGlobalTransforms.Num());
	//check(TargetBoneIndices.Num() == Params.Num());

	// Before setting this chain pose, we need to ensure that any
	// intermediate (between chains) NON-retargeted parent bones have had their
	// global transforms updated.
	// 
	// For example, if this chain is retargeting a single head bone, AND the spine was
	// retargeted in the prior step, then the neck bones will need updating first.
	// Otherwise the neck bones will remain at their location prior to the spine update.
	UpdateIntermediateParents(TargetSkeleton,InOutGlobalPose);

	// transform entire source chain from it's root to match target's current root orientation (maintaining offset from retarget pose)
	// this ensures children are retargeted in a "local" manner free from skewing that will happen if source and target
	// become misaligned as can happen if parent chains were not retargeted
	FTransform SourceChainParentInitialDelta = SourceChain.ChainParentInitialGlobalTransform.GetRelativeTransform(ChainParentInitialGlobalTransform);
	FTransform TargetChainParentCurrentGlobalTransform = ChainParentBoneIndex == INDEX_NONE ? FTransform::Identity : InOutGlobalPose[ChainParentBoneIndex]; 
	FTransform SourceChainParentTransform = SourceChainParentInitialDelta * TargetChainParentCurrentGlobalTransform;

	// apply delta to the source chain's current transforms before transferring rotations to the target
	SourceChain.TransformCurrentChainTransforms(SourceChainParentTransform);

	// if FK retargeting has been disabled for this chain, then simply set it to the retarget pose
	if (!Settings.FK.EnableFK)
	{
		// put the chain in the global ref pose (globally rotated by parent bone in it's currently retargeted state)
		PutCurrentTransformsInRefPose(TargetBoneIndices, TargetSkeleton, InOutGlobalPose);
		
		for (int32 ChainIndex=0; ChainIndex<TargetBoneIndices.size(); ++ChainIndex)
		{
			const int32 BoneIndex = TargetBoneIndices[ChainIndex];
			InOutGlobalPose[BoneIndex] = CurrentGlobalTransforms[ChainIndex];
		}

		return;
	}

	const int32 NumBonesInSourceChain = SourceChain.CurrentGlobalTransforms.size();
	const int32 NumBonesInTargetChain = TargetBoneIndices.size();
	const int32 TargetStartIndex = std::max(0, NumBonesInTargetChain - NumBonesInSourceChain);
	const int32 SourceStartIndex = std::max(0,NumBonesInSourceChain - NumBonesInTargetChain);

	// now retarget the pose of each bone in the chain, copying from source to target
	for (int32 ChainIndex=0; ChainIndex<TargetBoneIndices.size(); ++ChainIndex)
	{
		const int32 BoneIndex = TargetBoneIndices[ChainIndex];
		const FTransform& TargetInitialTransform = InitialGlobalTransforms[ChainIndex];
		FTransform SourceCurrentTransform;
		FTransform SourceInitialTransform;

		// get source current / initial transforms for this bone
		switch (Settings.FK.RotationMode)
		{
			case ERetargetRotationMode::Interpolated:
			{
				// get the initial and current transform of source chain at param
				// this is the interpolated transform along the chain
				const float Param = Params[ChainIndex];
					
				SourceCurrentTransform = GetTransformAtParam(
					SourceChain.CurrentGlobalTransforms,
					SourceChain.Params,
					Param);

				SourceInitialTransform = GetTransformAtParam(
					SourceChain.InitialGlobalTransforms,
					SourceChain.Params,
					Param);
			}
			break;
			case ERetargetRotationMode::OneToOne:
			{
				if (ChainIndex < NumBonesInSourceChain)
				{
					SourceCurrentTransform = SourceChain.CurrentGlobalTransforms[ChainIndex];
					SourceInitialTransform = SourceChain.InitialGlobalTransforms[ChainIndex];
				}else
				{
					SourceCurrentTransform = SourceChain.CurrentGlobalTransforms.back();
					SourceInitialTransform = SourceChain.InitialGlobalTransforms.back();
				}
			}
			break;
			case ERetargetRotationMode::OneToOneReversed:
			{
				if (ChainIndex < TargetStartIndex)
				{
					SourceCurrentTransform = SourceChain.InitialGlobalTransforms[0];
					SourceInitialTransform = SourceChain.InitialGlobalTransforms[0];
				}
				else
				{
					const int32 SourceChainIndex = SourceStartIndex + (ChainIndex - TargetStartIndex);
					SourceCurrentTransform = SourceChain.CurrentGlobalTransforms[SourceChainIndex];
					SourceInitialTransform = SourceChain.InitialGlobalTransforms[SourceChainIndex];
				}
			}
			break;
			case ERetargetRotationMode::None:
			{
				SourceCurrentTransform = SourceChain.InitialGlobalTransforms.back();
				SourceInitialTransform = SourceChain.InitialGlobalTransforms.back();
			}
			break;
			default:
				checkNoEntry();
			break;
		}
		
		// apply rotation offset to the initial target rotation
		const FQuat SourceCurrentRotation = SourceCurrentTransform.GetRotation();
		const FQuat SourceInitialRotation = SourceInitialTransform.GetRotation();
		const FQuat RotationDelta = SourceCurrentRotation * SourceInitialRotation.Inverse();
		const FQuat TargetInitialRotation = TargetInitialTransform.GetRotation();
		const FQuat OutRotation = RotationDelta * TargetInitialRotation;

		// calculate output POSITION based on translation mode setting
		FTransform ParentGlobalTransform = FTransform::Identity;
		const int32 ParentIndex = TargetSkeleton.ParentIndices[BoneIndex];
		if (ParentIndex != INDEX_NONE)
		{
			ParentGlobalTransform = InOutGlobalPose[ParentIndex];
		}
		FVector OutPosition;
		switch (Settings.FK.TranslationMode)
		{
			case ERetargetTranslationMode::None:
				{
					const FVector InitialLocalOffset = TargetSkeleton.RetargetLocalPose[BoneIndex].GetTranslation();
					OutPosition = ParentGlobalTransform.TransformPosition(InitialLocalOffset);
				}
				break;
			case ERetargetTranslationMode::GloballyScaled:
				{
					OutPosition = SourceCurrentTransform.GetTranslation() * RootRetargeter.GetGlobalScaleVector();
				}
				break;
			case ERetargetTranslationMode::Absolute:
				OutPosition = SourceCurrentTransform.GetTranslation();
				break;
			default:
				checkNoEntry();
				break;
		}

		// calculate output SCALE
		const FVector SourceCurrentScale = SourceCurrentTransform.GetScale3D();
		const FVector SourceInitialScale = SourceInitialTransform.GetScale3D();
		const FVector TargetInitialScale = TargetInitialTransform.GetScale3D();
		const FVector OutScale = SourceCurrentScale + (TargetInitialScale - SourceInitialScale);
		
		// apply output transform
		CurrentGlobalTransforms[ChainIndex] = FTransform(OutRotation, OutPosition, OutScale);
		InOutGlobalPose[BoneIndex] = CurrentGlobalTransforms[ChainIndex];
	}

	// apply final blending between retarget pose of chain and newly retargeted pose
	// blend must be done in local space, so we do it in a separate loop after full chain pose is generated
	// (skipped if the alphas are not near 1.0)
	if (!IsNearlyEqual(Settings.FK.RotationAlpha, 1.0f) || !IsNearlyEqual(Settings.FK.TranslationAlpha, 1.0f))
	{
		TArray<FTransform> NewLocalTransforms;
		NewLocalTransforms.resize(InitialLocalTransforms.size());
		FillTransformsWithLocalSpaceOfChain(TargetSkeleton, InOutGlobalPose, TargetBoneIndices, NewLocalTransforms);

		for (int32 ChainIndex=0; ChainIndex<InitialLocalTransforms.size(); ++ChainIndex)
		{
			// blend between current local pose and initial local pose
			FTransform& NewLocalTransform = NewLocalTransforms[ChainIndex];
			const FTransform& RefPoseLocalTransform = InitialLocalTransforms[ChainIndex];
			NewLocalTransform.SetTranslation(FVector::lerp(RefPoseLocalTransform.GetTranslation(), NewLocalTransform.GetTranslation(), Settings.FK.TranslationAlpha));
			NewLocalTransform.SetRotation(FQuat::FastLerp(RefPoseLocalTransform.GetRotation(), NewLocalTransform.GetRotation(), Settings.FK.RotationAlpha).GetNormalized());

			// put blended transforms back in global space and store in final output pose
			const int32 BoneIndex = TargetBoneIndices[ChainIndex];
			const int32 ParentIndex = TargetSkeleton.ParentIndices[BoneIndex];
			const FTransform& ParentGlobalTransform = ParentIndex == INDEX_NONE ? FTransform::Identity : InOutGlobalPose[ParentIndex];
			InOutGlobalPose[BoneIndex] = NewLocalTransform * ParentGlobalTransform;
		}
	}
}

void FChainDecoderFK::InitializeIntermediateParentIndices(
	const int32 RetargetRootBoneIndex,
	const int32 ChainRootBoneIndex,
	const FTargetSkeleton& TargetSkeleton)
{
	IntermediateParentIndices.clear();
	int32 ParentBoneIndex = TargetSkeleton.ParentIndices[ChainRootBoneIndex];
	while (true)
	{
		if (ParentBoneIndex < 0 || ParentBoneIndex == RetargetRootBoneIndex)
		{
			break; // reached root of skeleton
		}

		if (TargetSkeleton.IsBoneRetargeted[ParentBoneIndex])
		{
			break; // reached the start of another retargeted chain
		}

		IntermediateParentIndices.push_back(ParentBoneIndex);
		ParentBoneIndex = TargetSkeleton.ParentIndices[ParentBoneIndex];
	}

	std::reverse(IntermediateParentIndices.begin(), IntermediateParentIndices.end());
}

void FChainDecoderFK::UpdateIntermediateParents(
	const FTargetSkeleton& TargetSkeleton,
	TArray<FTransform>& InOutGlobalPose)
{
	for (const int32& ParentIndex : IntermediateParentIndices)
	{
		TargetSkeleton.UpdateGlobalTransformOfSingleBone(ParentIndex, TargetSkeleton.RetargetLocalPose, InOutGlobalPose);
	}
}

FTransform FChainDecoderFK::GetTransformAtParam(
	const TArray<FTransform>& Transforms,
	const TArray<float>& InParams,
	const float& Param) const
{
	if (InParams.size() == 1)
	{
		return Transforms[0];
	}
	
	if (Param < KINDA_SMALL_NUMBER)
	{
		return Transforms[0];
	}

	if (Param > 1.0f - KINDA_SMALL_NUMBER)
	{
		return Transforms.back();
	}

	for (int32 ChainIndex=1; ChainIndex<InParams.size(); ++ChainIndex)
	{
		const float CurrentParam = InParams[ChainIndex];
		if (CurrentParam <= Param)
		{
			continue;
		}
		
		const float PrevParam = InParams[ChainIndex-1];
		const float PercentBetweenParams = (Param - PrevParam) / (CurrentParam - PrevParam);
		const FTransform& Prev = Transforms[ChainIndex-1];
		const FTransform& Next = Transforms[ChainIndex];
		const FVector Position = FVector::lerp(Prev.GetTranslation(), Next.GetTranslation(), PercentBetweenParams);
		const FQuat Rotation = FQuat::FastLerp(Prev.GetRotation(), Next.GetRotation(), PercentBetweenParams).GetNormalized();
		const FVector Scale = FVector::lerp(Prev.GetScale3D(), Next.GetScale3D(), PercentBetweenParams);
		
		return FTransform(Rotation,Position, Scale);
	}

	checkNoEntry();
	return FTransform::Identity;
}


bool FChainRetargeterIK::InitializeSource(
	const TArray<int32>& BoneIndices,
	const TArray<FTransform>& SourceInitialGlobalPose,
	FIKRigLogger& Log)
{
	// todo
	return true;
}

void FChainRetargeterIK::EncodePose(const TArray<FTransform>& InSourceGlobalPose)
{
	// todo
}

bool FChainRetargeterIK::InitializeTarget(
	const TArray<int32>& BoneIndices,
	const TArray<FTransform> &TargetInitialGlobalPose,
	FIKRigLogger& Log)
{
	// todo
	return true;
}
	
void FChainRetargeterIK::DecodePose(
	const FTargetChainSettings& Settings,
	const FRootRetargeter& RootRetargeter,
	const std::unordered_map<FName, float>& SpeedValuesFromCurves,
	const float DeltaTime,
    const TArray<FTransform>& InGlobalPose)
{
	// todo
}

// MARK: - chain pair


bool FRetargetChainPair::Initialize(
    const FBoneChain& SourceBoneChain,
    const FBoneChain& TargetBoneChain,
    const FRetargetSkeleton& SourceSkeleton,
    const FTargetSkeleton& TargetSkeleton,
    FIKRigLogger& Log)
{
	// validate source bone chain is compatible with source skeletal mesh
	const bool bIsSourceValid = ValidateBoneChainWithSkeletalMesh(true, SourceBoneChain, SourceSkeleton, Log);
	if (!bIsSourceValid)
	{
		Log.LogWarning("IncompatibleSourceChain, IK Retargeter source bone chain, '%s', is not compatible with Skeletal Mesh: '%s'",
			SourceBoneChain.ChainName.c_str(), SourceSkeleton.Skeleton->GetName().c_str());
		return false;
	}

	// validate target bone chain is compatible with target skeletal mesh
	const bool bIsTargetValid = ValidateBoneChainWithSkeletalMesh(false, TargetBoneChain, TargetSkeleton, Log);
	if (!bIsTargetValid)
    {
		Log.LogWarning("IncompatibleTargetChain, IK Retargeter target bone chain, '{%s}', is not compatible with Skeletal Mesh: '{%s}'",
			TargetBoneChain.ChainName.c_str(), TargetSkeleton.Skeleton->GetName().c_str());
		return false;
    }

	// store attributes of chain
	SourceBoneChainName = SourceBoneChain.ChainName;
	TargetBoneChainName = TargetBoneChain.ChainName;
	
	return true;
}

bool FRetargetChainPair::ValidateBoneChainWithSkeletalMesh(
    const bool IsSource,
    const FBoneChain& BoneChain,
    const FRetargetSkeleton& RetargetSkeleton,
    FIKRigLogger& Log)
{
	// record the chain indices
	TArray<int32>& BoneIndices = IsSource ? SourceBoneIndices : TargetBoneIndices;
	
	// resolve the bone bone to the skeleton
	const FResolvedBoneChain ResolvedChain = FResolvedBoneChain(BoneChain, RetargetSkeleton, BoneIndices);
	
	// warn if START bone not found
	if (!ResolvedChain.bFoundStartBone)
	{
		Log.LogWarning("MissingStartBone, IK Retargeter bone chain, {%s}, could not find start bone, {%s} in mesh {%s}",
			BoneChain.ChainName.c_str(),
			BoneChain.StartBone.BoneName.c_str(),
			RetargetSkeleton.Skeleton->GetName().c_str());
	}
	
	// warn if END bone not found
	if (!ResolvedChain.bFoundEndBone)
	{
		Log.LogWarning("MissingEndBone, IK Retargeter bone chain, {%s}, could not find end bone, {%s} in mesh {%s}",
			FText::FromName(BoneChain.ChainName), 
			FText::FromName(BoneChain.EndBone.BoneName), 
			FText::FromName(RetargetSkeleton.Skeleton->GetName()));
	}

	// warn if END bone was not a child of START bone
	if (ResolvedChain.bFoundEndBone && !ResolvedChain.bEndIsStartOrChildOfStart)
	{
		Log.LogWarning("EndNotChildtOfStart, IK Retargeter bone chain, {%s}, end bone, '{%s}' was not a child of the start bone '{%s}'.",
			FText::FromName(BoneChain.ChainName), FText::FromName(BoneChain.EndBone.BoneName), FText::FromName(BoneChain.StartBone.BoneName));
	}
	
	return ResolvedChain.IsValid();
}

bool FRetargetChainPairFK::Initialize(
	const FBoneChain& SourceBoneChain,
	const FBoneChain& TargetBoneChain,
	const FRetargetSkeleton& SourceSkeleton,
	const FTargetSkeleton& TargetSkeleton,
	FIKRigLogger& Log)
{
	const bool bChainInitialized = FRetargetChainPair::Initialize(SourceBoneChain, TargetBoneChain, SourceSkeleton, TargetSkeleton, Log);
	if (!bChainInitialized)
	{
		return false;
	}

	// initialize SOURCE FK chain encoder with retarget pose
	const bool bFKEncoderInitialized = FKEncoder.Initialize(SourceSkeleton, SourceBoneIndices, SourceSkeleton.RetargetGlobalPose, Log);
	if (!bFKEncoderInitialized)
	{
		Log.LogWarning( "BadFKEncoder, IK Retargeter failed to initialize FK encoder, '{%s}', on Skeletal Mesh: '{%s}'",
			FText::FromName(SourceBoneChainName), FText::FromName(SourceSkeleton.Skeleton->GetName()));
		return false;
	}

	// initialize TARGET FK chain decoder with retarget pose
	const bool bFKDecoderInitialized = FKDecoder.Initialize(TargetSkeleton, TargetBoneIndices, TargetSkeleton.RetargetGlobalPose, Log);
	if (!bFKDecoderInitialized)
	{
		Log.LogWarning("BadFKDecoder, IK Retargeter failed to initialize FK decoder, '{%s}', on Skeletal Mesh: '{%s}'",
			FText::FromName(TargetBoneChainName), FText::FromName(TargetSkeleton.Skeleton->GetName()));
		return false;
	}

	// initialize the pole vector matcher for this chain
	// const bool bPoleVectorMatcherInitialized = PoleVectorMatcher.Initialize(
	// 	SourceBoneIndices,
	// 	TargetBoneIndices,
	// 	SourceSkeleton.RetargetGlobalPose,
	// 	TargetSkeleton.RetargetGlobalPose,
	// 	TargetSkeleton);
	// if (!bPoleVectorMatcherInitialized)
	// {
	// 	Log.LogWarning( FText::Format(
	// 		LOCTEXT("BadPoleVectorMatcher", "IK Retargeter failed to initialize pole matching for chain, '{0}', on Skeletal Mesh: '{1}'"),
	// 		FText::FromName(TargetBoneChainName), FText::FromString(TargetSkeleton.SkeletalMesh->GetName())));
	// 	return false;
	// }

	return true;
}



bool FRetargetChainPairIK::Initialize(
	const FBoneChain& SourceBoneChain,
	const FBoneChain& TargetBoneChain,
	const FRetargetSkeleton& SourceSkeleton,
	const FTargetSkeleton& TargetSkeleton,
	FIKRigLogger& Log)
{
	// todo

	return true;
}


// MARK: - root retargeter

bool FRootRetargeter::InitializeSource(
	const FName SourceRootBoneName,
	const FRetargetSkeleton& SourceSkeleton,
	FIKRigLogger& Log)
{
	// validate target root bone exists
	Source.BoneName = SourceRootBoneName;
	Source.BoneIndex = SourceSkeleton.FindBoneIndexByName(SourceRootBoneName);
	if (Source.BoneIndex == INDEX_NONE)
	{
		//Log.LogWarning("MissingSourceRoot", "IK Retargeter could not find source root bone, {0} in mesh {1}"),
		//	FText::FromName(SourceRootBoneName), FText::FromString(SourceSkeleton.SkeletalMesh->GetName())));
		return false;
	}
	
	// record initial root data
	const FTransform InitialTransform = SourceSkeleton.RetargetGlobalPose[Source.BoneIndex]; 
	float InitialHeight = InitialTransform.GetTranslation().z;
	Source.InitialRotation = InitialTransform.GetRotation();

	// ensure root height is not at origin, this happens if user sets root to ACTUAL skeleton root and not pelvis
	if (InitialHeight < KINDA_SMALL_NUMBER)
	{
		// warn user and push it up slightly to avoid divide by zero
		Log.LogError("BadRootHeight, The source retarget root bone is very near the ground plane. This will cause the target to be moved very far. To resolve this, please create a retarget pose with the retarget root at the correct height off the ground.");
		InitialHeight = 1.0f;
	}

	// invert height
	Source.InitialHeightInverse = 1.0f / InitialHeight;

	return true;
}

bool FRootRetargeter::InitializeTarget(
	const FName TargetRootBoneName,
	const FTargetSkeleton& TargetSkeleton,
	FIKRigLogger& Log)
{
	// validate target root bone exists
	Target.BoneName = TargetRootBoneName;
	Target.BoneIndex = TargetSkeleton.FindBoneIndexByName(TargetRootBoneName);
	if (Target.BoneIndex == INDEX_NONE)
	{
		//Log.LogWarning( FText::Format(
		//	LOCTEXT("CountNotFindRootBone", "IK Retargeter could not find target root bone, {0} in mesh {1}"),
		//	FText::FromName(TargetRootBoneName), FText::FromString(TargetSkeleton.SkeletalMesh->GetName())));
		return false;
	}

	const FTransform TargetInitialTransform = TargetSkeleton.RetargetGlobalPose[Target.BoneIndex];
	Target.InitialHeight = TargetInitialTransform.GetTranslation().z;
	Target.InitialRotation = TargetInitialTransform.GetRotation();
	Target.InitialPosition = TargetInitialTransform.GetTranslation();

	// initialize the global scale factor
	const float ScaleFactor = Source.InitialHeightInverse * Target.InitialHeight;
	GlobalScaleFactor.Set(ScaleFactor, ScaleFactor, ScaleFactor);
	
	return true;
}

void FRootRetargeter::Reset()
{
	Source = FRootSource();
	Target = FRootTarget();
}

void FRootRetargeter::EncodePose(const TArray<FTransform>& SourceGlobalPose)
{
	const FTransform& SourceTransform = SourceGlobalPose[Source.BoneIndex];
	Source.CurrentPosition = SourceTransform.GetTranslation();
	Source.CurrentPositionNormalized = Source.CurrentPosition * Source.InitialHeightInverse;
	Source.CurrentRotation = SourceTransform.GetRotation();	
}

void FRootRetargeter::DecodePose(TArray<FTransform>& OutTargetGlobalPose)
{
	// retarget position
	FVector Position;
	{
		// generate basic retarget root position by scaling the normalized position by root height
		const FVector RetargetedPosition = Source.CurrentPositionNormalized * Target.InitialHeight;
		
		// blend the retarget root position towards the source retarget root position
		Position = FVector::lerp(RetargetedPosition, Source.CurrentPosition, Settings.BlendToSource*Settings.BlendToSourceWeights);

		// apply vertical / horizontal scaling of motion
		FVector ScaledRetargetedPosition = Position;
		ScaledRetargetedPosition.z *= Settings.ScaleVertical;
		const FVector HorizontalOffset = (ScaledRetargetedPosition - Target.InitialPosition) * FVector(Settings.ScaleHorizontal, Settings.ScaleHorizontal, 1.0f);
		Position = Target.InitialPosition + HorizontalOffset;
		
		// apply a static offset
		Position += Settings.TranslationOffset;

		// blend with alpha
		Position = FVector::lerp(Target.InitialPosition, Position, Settings.TranslationAlpha);

		// record the delta created by all the modifications made to the root translation
		Target.RootTranslationDelta = Position - RetargetedPosition;
	}

	// retarget rotation
	FQuat Rotation;
	{
		// calc offset between initial source/target root rotations
		const FQuat RotationDelta = Source.CurrentRotation * Source.InitialRotation.Inverse();
		// add retarget pose delta to the current source rotation
		const FQuat RetargetedRotation = RotationDelta * Target.InitialRotation;

		// add static rotation offset
		Rotation = RetargetedRotation * Settings.RotationOffset.Quaternion();

		// blend with alpha
		Rotation = FQuat::FastLerp(Target.InitialRotation, Rotation, Settings.RotationAlpha);
		Rotation.Normalize();

		// record the delta created by all the modifications made to the root rotation
		Target.RootRotationDelta = RetargetedRotation * Target.InitialRotation.Inverse();
	}

	// apply to target
	FTransform& TargetRootTransform = OutTargetGlobalPose[Target.BoneIndex];
	TargetRootTransform.SetTranslation(Position);
	TargetRootTransform.SetRotation(Rotation);
}




UIKRetargetProcessor::UIKRetargetProcessor()
{
}

// MARK: - init
/* #region MARK: -init */

void UIKRetargetProcessor::Initialize(
		USkeleton* InSourceSkeleton,
		USkeleton* InTargetSkeleton,
		UIKRetargeter* InRetargeterAsset,
		const bool bSuppressWarnings)
{
	// reset all initialized flags
	bIsInitialized = false;
	bRootsInitialized = false;
	bAtLeastOneValidBoneChainPair = false;
	bIKRigInitialized = false;
	
	// record source asset
	RetargeterAsset = InRetargeterAsset;

	const UIKRigDefinition* SourceIKRig = RetargeterAsset->GetSourceIKRig();
	const UIKRigDefinition* TargetIKRig = RetargeterAsset->GetTargetIKRig();

	// check prerequisite assets
	if (!InSourceSkeleton) {
		Log.LogError("MissingSourceSkeleton");
		return;
	}
	if (!InTargetSkeleton) {
		Log.LogError("MissingTargetSkeleton");
		return;
	}
	if (!SourceIKRig) {
		Log.LogError("MissingSourceIKRig");
		return;
	}
	if (!TargetIKRig) {
		Log.LogError("MissingTargetIKRig");
		return;
	}
	

	// ref pose on ref pose, no retarget pose
	FName InRetargetPoseName = "None";
	FIKRetargetPose* RetargetPose = nullptr;

	// initialize skeleton data for source and target
	SourceSkeleton.Initialize(
		InSourceSkeleton,
		SourceIKRig->GetRetargetChains(),
		InRetargetPoseName,
		RetargetPose,
		//RetargeterAsset->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source),
		//RetargeterAsset->GetCurrentRetargetPose(ERetargetSourceOrTarget::Source),
		SourceIKRig->GetRetargetRoot());
	
	TargetSkeleton.Initialize(
		InTargetSkeleton,
		TargetIKRig->GetRetargetChains(),
		InRetargetPoseName,//RetargeterAsset->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target),
		RetargetPose,//RetargeterAsset->GetCurrentRetargetPose(ERetargetSourceOrTarget::Target),
		TargetIKRig->GetRetargetRoot());

	// initialize roots
	bRootsInitialized = InitializeRoots();

	// initialize pairs of bone chains
	bAtLeastOneValidBoneChainPair = InitializeBoneChainPairs();
	if (!bAtLeastOneValidBoneChainPair)
	{
		// couldn't match up any BoneChain pairs, no limb retargeting possible
		// Log.LogWarning( 
		// 	"NoMappedChains, unable to map any bone chains between source, %s and target, %s",
		// 	SourceSkeletalMesh->GetName(), TargetSkeletalMesh->GetName());
	}

	// initialize the IKRigProcessor for doing IK decoding
	bIKRigInitialized = InitializeIKRig(this, InTargetSkeleton);
	if (!bIKRigInitialized)
	{
		// couldn't initialize the IK Rig, we don't disable the retargeter in this case, just warn the user
		// Log.LogWarning( 
        //     "CouldNotInitializeIKRig, unable to initialize the IK Rig, %s for the Skeletal Mesh %s.",
		// 	TargetIKRig->GetName(), TargetSkeletalMesh->GetName());
	}

	// must have a mapped root bone OR at least a single mapped chain to be able to do any retargeting at all
	if (bRootsInitialized && bAtLeastOneValidBoneChainPair)
	{
		// confirm for the user that the IK Rig was initialized successfully
		// Log.LogInfo(
        //     "SuccessfulInit, Success! ready to transfer animation from the source, %s to the target, %s",
		// 		SourceSkeleton->GetName(), TargetSkeleton->GetName());
	}

	// copy the initial settings from the asset
	//ApplySettingsFromAsset();
	
	bIsInitialized = true;
}

bool UIKRetargetProcessor::InitializeRoots()
{
	// reset root data
	RootRetargeter.Reset();
	
	// initialize root encoder
	const FName SourceRootBoneName = RetargeterAsset->GetSourceIKRig()->GetRetargetRoot();
	const bool bRootEncoderInit = RootRetargeter.InitializeSource(SourceRootBoneName, SourceSkeleton, Log);
	if (!bRootEncoderInit)
	{
		//Log.LogWarning( FText::Format(
		//	LOCTEXT("NoSourceRoot", "IK Retargeter unable to initialize source root, '{0}' on skeletal mesh: '{1}'"),
		//	FText::FromName(SourceRootBoneName), FText::FromString(SourceSkeleton.SkeletalMesh->GetName())));
	}

	// initialize root decoder
	const FName TargetRootBoneName = RetargeterAsset->GetTargetIKRig()->GetRetargetRoot();
	const bool bRootDecoderInit = RootRetargeter.InitializeTarget(TargetRootBoneName, TargetSkeleton, Log);
	if (!bRootDecoderInit)
	{
		// Log.LogWarning( FText::Format(
		// 	LOCTEXT("NoTargetRoot", "IK Retargeter unable to initialize target root, '{0}' on skeletal mesh: '{1}'"),
		// 	FText::FromName(TargetRootBoneName), FText::FromString(TargetSkeleton.SkeletalMesh->GetName())));
	}

	return bRootEncoderInit && bRootDecoderInit;
}

bool UIKRetargetProcessor::InitializeBoneChainPairs()
{
	ChainPairsFK.clear();
	ChainPairsIK.clear();
	
	const UIKRigDefinition* TargetIKRig = RetargeterAsset->GetTargetIKRig();
	const UIKRigDefinition* SourceIKRig = RetargeterAsset->GetSourceIKRig();
	
	//check(SourceIKRig && TargetIKRig);

	// check that chains are available in both IKRig assets before sorting them based on StartBone index
	const TArray<std::shared_ptr<URetargetChainSettings>>& ChainMapping = RetargeterAsset->GetAllChainSettings();	
	for (std::shared_ptr<URetargetChainSettings> ChainMap : ChainMapping)
	{
		// get target bone chain
		const FBoneChain* TargetBoneChain = RetargeterAsset->GetTargetIKRig()->GetRetargetChainByName(ChainMap->TargetChain);
		if (!TargetBoneChain)
		{
			// Log.LogWarning( FText::Format(
			// LOCTEXT("MissingTargetChain", "IK Retargeter missing target bone chain: {0}. Please update the mapping."),
			// FText::FromString(ChainMap->TargetChain.ToString())));
			continue;
		}
		
		// user opted to not map this to anything, we don't need to spam a warning about it
		if (ChainMap->SourceChain == NAME_None)
		{
			continue; 
		}
		
		// get source bone chain
		const FBoneChain* SourceBoneChain = RetargeterAsset->GetSourceIKRig()->GetRetargetChainByName(ChainMap->SourceChain);
		if (!SourceBoneChain)
		{
			// Log.LogWarning( FText::Format(
			// LOCTEXT("MissingSourceChain", "IK Retargeter missing source bone chain: {0}"),
			// FText::FromString(ChainMap->SourceChain.ToString())));
			continue;
		}

		// all chains are loaded as FK (giving IK better starting pose)
		FRetargetChainPairFK ChainPair;
		if (ChainPair.Initialize(*SourceBoneChain, *TargetBoneChain, SourceSkeleton, TargetSkeleton, Log))
		{
			ChainPairsFK.push_back(ChainPair);
		}
		
		// load IK chain
		if (ChainMap->Settings.IK.EnableIK)
		{
			FRetargetChainPairIK ChainPairIK;
			if (ChainPairIK.Initialize(*SourceBoneChain, *TargetBoneChain, SourceSkeleton, TargetSkeleton, Log))
			{
				ChainPairsIK.push_back(ChainPairIK);
			}
		}

		// todo ik
		// warn user if IK goal is not on the END bone of the target chain. It will still work, but may produce bad results.
		// const TArray<UIKRigEffectorGoal*> AllGoals = TargetIKRig->GetGoalArray();
		// for (const UIKRigEffectorGoal*Goal : AllGoals)
		// {
		// 	if (Goal->GoalName == TargetBoneChain->IKGoalName)
		// 	{
		// 		if (Goal->BoneName != TargetBoneChain->EndBone.BoneName)
		// 		{
		// 		// 	Log.LogWarning( FText::Format(
		// 		// LOCTEXT("TargetIKNotOnEndBone", "Retarget chain, '{0}' has an IK goal that is not on the End Bone of the chain."),
		// 		// 	FText::FromString(TargetBoneChain->ChainName.ToString())));
		// 		}
		// 		break;
		// 	}
		// }
	}

	// sort the chains based on their StartBone's index
	auto ChainsSorter = [this](const FRetargetChainPair& A, const FRetargetChainPair& B)
	{
		const int32 IndexA = A.TargetBoneIndices.size() > 0 ? A.TargetBoneIndices[0] : INDEX_NONE;
		const int32 IndexB = B.TargetBoneIndices.size() > 0 ? B.TargetBoneIndices[0] : INDEX_NONE;
		if (IndexA == IndexB)
		{
			// fallback to sorting alphabetically
			return A.TargetBoneChainName.compare(B.TargetBoneChainName) <= 0;
		}
		return IndexA < IndexB;
	};
	
	std::sort(ChainPairsFK.begin(), ChainPairsFK.end(), ChainsSorter);
	std::sort(ChainPairsIK.begin(), ChainPairsIK.end(), ChainsSorter);
	
	// record which bones in the target skeleton are being retargeted
	for (const FRetargetChainPairFK& FKChainPair : ChainPairsFK)
	{
		for (const int32 BoneIndex : FKChainPair.TargetBoneIndices)
		{
			TargetSkeleton.SetBoneIsRetargeted(BoneIndex, true);
		}
	}

	// record intermediate bones (non-retargeted bones located BETWEEN FK chains on the target skeleton)
	for (FRetargetChainPairFK& FKChainPair : ChainPairsFK)
	{
		FKChainPair.FKDecoder.InitializeIntermediateParentIndices(
			RootRetargeter.Target.BoneIndex,
			FKChainPair.TargetBoneIndices[0],
			TargetSkeleton);
	}

	// root is updated before IK as well
	if (bRootsInitialized)
	{
		TargetSkeleton.SetBoneIsRetargeted(RootRetargeter.Target.BoneIndex, true);	
	}

	// return true if at least 1 pair of bone chains were initialized
	return !(ChainPairsIK.empty() && ChainPairsFK.empty());
}

bool UIKRetargetProcessor::InitializeIKRig(UObject* Outer, const USkeleton* InSkeleton)
{
	// TODO	
	return true;
}


/* #endregion */



// MARK: - run retarget
/* #region MARK: -run retarget */

TArray<FTransform>&  UIKRetargetProcessor::RunRetargeter(
	const TArray<FTransform>& InSourceGlobalPose,
	const std::unordered_map<FName, float>& SpeedValuesFromCurves,
	const float DeltaTime)
{
	//check(bIsInitialized);

	Log.LogWarning("MissingSourceMesh,UIKRetargetProcessor run retargeter");
	
	// start from retarget pose
	TargetSkeleton.OutputGlobalPose = TargetSkeleton.RetargetGlobalPose;

	// ROOT retargeting
	if (GlobalSettings.bEnableRoot && bRootsInitialized)
	{
		RunRootRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
		// update global transforms below root
		TargetSkeleton.UpdateGlobalTransformsBelowBone(RootRetargeter.Target.BoneIndex, TargetSkeleton.RetargetLocalPose, TargetSkeleton.OutputGlobalPose);
	}
	
	// FK CHAIN retargeting
	if (GlobalSettings.bEnableFK && bAtLeastOneValidBoneChainPair)
	{
		RunFKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
		// update all the bones that are not controlled by FK chains or root
		TargetSkeleton.UpdateGlobalTransformsAllNonRetargetedBones(TargetSkeleton.OutputGlobalPose);
	}
	
	// IK CHAIN retargeting
	if (GlobalSettings.bEnableIK && bAtLeastOneValidBoneChainPair && bIKRigInitialized)
	{
		RunIKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose, SpeedValuesFromCurves, DeltaTime);
	}

	// Pole Vector matching between source / target chains
	if (GlobalSettings.bEnableFK && bAtLeastOneValidBoneChainPair)
	{
		RunPoleVectorMatching(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
	}

	return TargetSkeleton.OutputGlobalPose;
}

void UIKRetargetProcessor::RunRootRetarget(
	const TArray<FTransform>& InGlobalTransforms,
    TArray<FTransform>& OutGlobalTransforms)
{
	RootRetargeter.EncodePose(InGlobalTransforms);
	RootRetargeter.DecodePose(OutGlobalTransforms);
}

void UIKRetargetProcessor::RunFKRetarget(
	const TArray<FTransform>& InGlobalTransforms,
    TArray<FTransform>& OutGlobalTransforms)
{
	// spin through chains and encode/decode them all using the input pose
	for (FRetargetChainPairFK& ChainPair : ChainPairsFK)
	{
		ChainPair.FKEncoder.EncodePose(
			SourceSkeleton,
			ChainPair.SourceBoneIndices,
			InGlobalTransforms);
		
		ChainPair.FKDecoder.DecodePose(
			RootRetargeter,
			ChainPair.Settings,
			ChainPair.TargetBoneIndices,
			ChainPair.FKEncoder,
			TargetSkeleton,
			OutGlobalTransforms);
	}
}

void UIKRetargetProcessor::RunIKRetarget(
	const TArray<FTransform>& InSourceGlobalPose,
    TArray<FTransform>& OutTargetGlobalPose,
    const std::unordered_map<FName, float>& SpeedValuesFromCurves,
    const float DeltaTime)
{
	// todo
}

void UIKRetargetProcessor::RunStrideWarping(const TArray<FTransform>& InTargetGlobalPose)
{
	// todo
}

/* #endregion */