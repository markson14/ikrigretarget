//
//  main.cpp
//  test libGLVNDGLContext
//
//  Created by kai chen on 2/8/23.
//

#include <stdio.h>

#include "SoulScene.hpp"
#include "SoulRetargeter.h"
#include "IKRigUtils.hpp"
#include "SoulIKRetargetProcessor.h"

#include "FBXRW.h"
#include "ObjRW.h"
#include "InitPoseConvert.h"

using namespace SoulIK;

//#define DEBUG_POSE_PRINT

// generate pose of every joint and every frame
static void buildPoseAnimationByInterpolation(SoulScene& scene, SoulSkeletonMesh& skmesh,
                        SoulJointAnimation& animation, std::vector<SoulIK::SoulPose>& poses) {

    std::vector<SoulIK::SoulTransform> refpose(skmesh.skeleton.joints.size()); 
    IKRigUtils::getSoulPoseFromMesh(scene, skmesh, refpose);

    std::vector<SoulAniChannel> channelsSparse(skmesh.skeleton.joints.size());  // [jointId][frame]
    std::vector<SoulAniChannel> channelsDense(skmesh.skeleton.joints.size());   // [jointId][frame]

    // copy
    for(int i = 0; i < skmesh.animation.channels.size(); i++) {
        SoulAniChannel& channel = skmesh.animation.channels[i];
        uint32_t jointId = channel.jointId;
        channelsSparse[jointId] = channel;
    }

    // interpolation
    std::vector<SoulIK::SoulTransform> curpose = refpose;
    for(int jointId = 0; jointId < skmesh.skeleton.joints.size(); jointId++) {
        
        int prevFrame = 0;
        int curFrame = 0;
        int frameCount = skmesh.animation.duration + 1;
        
        // position
        if (channelsSparse[jointId].PositionKeys.size() == 0) {
            auto& DenseKeys = channelsDense[jointId].PositionKeys;
            SoulVec3Key key{0, refpose[jointId].translation};
            DenseKeys.resize(frameCount, key);
            for(int frame = 0; frame < skmesh.animation.duration; frame++) {
                DenseKeys[frame].time = frame;
            }
        } else {
            auto& SparseKeys = channelsSparse[jointId].PositionKeys;
            auto& DenseKeys = channelsDense[jointId].PositionKeys;
            DenseKeys.resize(frameCount);

            prevFrame = 0;
            glm::vec3 prevValue = SparseKeys[0].value;
            glm::vec3 curValue;
            for (int j = 0; j < SparseKeys.size(); j++) {
                curFrame = SparseKeys[j].time;
                curValue = SparseKeys[j].value;
                for(int frame = prevFrame; frame <= curFrame; frame++) {
                    DenseKeys[frame].time = frame;
                    float alpha = prevFrame == curFrame ? 1 : (frame - prevFrame) / (curFrame - prevFrame); 
                    DenseKeys[frame].value = glm::mix(prevValue, curValue, alpha);
                }

                // next iteration
                prevFrame = curFrame;
                prevValue = curValue;
            }
            // tail
            for(int frame = prevFrame+1; frame < frameCount; frame++) {
                DenseKeys[frame].time = frame;
                DenseKeys[frame].value = curValue;
            }
        }

        // scale
        if (channelsSparse[jointId].ScalingKeys.size() == 0) {
            auto& DenseKeys = channelsDense[jointId].ScalingKeys;
            SoulVec3Key key{0, refpose[jointId].scale};
            DenseKeys.resize(frameCount, key);
            for(int frame = 0; frame < skmesh.animation.duration; frame++) {
                DenseKeys[frame].time = frame;
            }
        } else {
            auto& SparseKeys = channelsSparse[jointId].ScalingKeys;
            auto& DenseKeys = channelsDense[jointId].ScalingKeys;
            DenseKeys.resize(frameCount);

            prevFrame = 0;
            glm::vec3 prevValue = SparseKeys[0].value;
            glm::vec3 curValue;
            for (int j = 0; j < SparseKeys.size(); j++) {
                curFrame = SparseKeys[j].time;
                curValue = SparseKeys[j].value;
                for(int frame = prevFrame; frame <= curFrame; frame++) {
                    DenseKeys[frame].time = frame;
                    float alpha = prevFrame == curFrame ? 1 : (frame - prevFrame) / (curFrame - prevFrame); 
                    DenseKeys[frame].value = glm::mix(prevValue, curValue, alpha);
                }

                // next iteration
                prevFrame = curFrame;
                prevValue = curValue;
            }
            // tail
            for(int frame = prevFrame+1; frame < frameCount; frame++) {
                DenseKeys[frame].time = frame;
                DenseKeys[frame].value = curValue;
            }
        }

        // rotation
        if (channelsSparse[jointId].RotationKeys.size() == 0) {
            auto& DenseKeys = channelsDense[jointId].RotationKeys;
            SoulQuatKey key{0, refpose[jointId].rotation};
            DenseKeys.resize(frameCount, key);
            for(int frame = 0; frame < skmesh.animation.duration; frame++) {
                DenseKeys[frame].time = frame;
            }
        } else {
            auto& SparseKeys = channelsSparse[jointId].RotationKeys;
            auto& DenseKeys = channelsDense[jointId].RotationKeys;
            DenseKeys.resize(frameCount);

            prevFrame = 0;
            glm::quat prevValue = SparseKeys[0].value;
            glm::quat curValue;
            for (int j = 0; j < SparseKeys.size(); j++) {
                curFrame = SparseKeys[j].time;
                curValue = SparseKeys[j].value;
                for(int frame = prevFrame; frame <= curFrame; frame++) {
                    DenseKeys[frame].time = frame;
                    float alpha = prevFrame == curFrame ? 1 : (frame - prevFrame) / (curFrame - prevFrame); 
                    DenseKeys[frame].value = glm::lerp(prevValue, curValue, alpha);
                }

                // next iteration
                prevFrame = curFrame;
                prevValue = curValue;
            }
            // tail
            for(int frame = prevFrame+1; frame < frameCount; frame++) {
                DenseKeys[frame].time = frame;
                DenseKeys[frame].value = curValue;
            }
        }
    }

    // pose
    poses.resize(skmesh.animation.duration);
    int frameCount = skmesh.animation.duration;
    for(int frame = 0; frame < frameCount; frame++) {
        SoulPose& pose = poses[frame];
        pose.transforms.resize(skmesh.skeleton.joints.size());
        for(int jointId = 0; jointId < skmesh.skeleton.joints.size(); jointId++) {
            pose.transforms[jointId].translation = channelsDense[jointId].PositionKeys[frame].value;
            pose.transforms[jointId].scale = channelsDense[jointId].ScalingKeys[frame].value;
            pose.transforms[jointId].rotation = channelsDense[jointId].RotationKeys[frame].value;
        }
    }
}

static void writePoseAnimationToMesh(std::vector<SoulIK::SoulPose>& tempoutposes, SoulIK::SoulSkeletonMesh& tgtskm, double duration, double ticksPerSecond) {

    // cannot save if no name
    tgtskm.animation.name = "mesh0";
    tgtskm.animation.duration = duration;
    tgtskm.animation.ticksPerSecond = ticksPerSecond;
    tgtskm.animation.channels.clear();
    tgtskm.animation.channels.resize(tgtskm.skeleton.joints.size());

    for(uint32_t jointId = 0; jointId < tgtskm.skeleton.joints.size(); jointId++) {
        tgtskm.animation.channels[jointId].jointId = jointId;
        auto frameCount = tempoutposes.size();
        // position
        tgtskm.animation.channels[jointId].PositionKeys.resize(frameCount);
        for(uint64_t frame = 0; frame < tempoutposes.size(); frame++) {
            tgtskm.animation.channels[jointId].PositionKeys[frame].time = frame;
            tgtskm.animation.channels[jointId].PositionKeys[frame].value =  tempoutposes[frame].transforms[jointId].translation;
        }

        tgtskm.animation.channels[jointId].ScalingKeys.resize(frameCount);
        for(uint64_t frame = 0; frame < tempoutposes.size(); frame++) {
            tgtskm.animation.channels[jointId].ScalingKeys[frame].time = frame;
            tgtskm.animation.channels[jointId].ScalingKeys[frame].value =  tempoutposes[frame].transforms[jointId].scale;
        }

        tgtskm.animation.channels[jointId].RotationKeys.resize(frameCount);
        for(uint64_t frame = 0; frame < tempoutposes.size(); frame++) {
            tgtskm.animation.channels[jointId].RotationKeys[frame].time = frame;
            tgtskm.animation.channels[jointId].RotationKeys[frame].value =  tempoutposes[frame].transforms[jointId].rotation;
        }        
    }
}

static std::shared_ptr<UIKRetargeter> createIKRigAsset(SoulIKRigRetargetConfig& config,
        SoulSkeleton& srcsk, SoulSkeleton& tgtsk, USkeleton& srcusk, USkeleton& tgtusk) {

    std::shared_ptr<UIKRetargeter> pInRetargeterAsset = std::make_shared<UIKRetargeter>();
    UIKRetargeter& InRetargeterAsset = *pInRetargeterAsset;
    
    ///////////////////////////////////////
    // ikrig 1
    InRetargeterAsset.SourceIKRigAsset = std::make_shared<UIKRigDefinition>();
    
    // skeleton
    FIKRigSkeleton rigsk;
    IKRigUtils::USkeleton2RigSkeleton(srcusk, rigsk);
    InRetargeterAsset.SourceIKRigAsset->Skeleton = rigsk;

    // rootbone
    InRetargeterAsset.SourceIKRigAsset->RetargetDefinition.RootBone = config.SourceRootBone;
    InRetargeterAsset.SourceIKRigAsset->RetargetDefinition.GroundBone = config.SourceGroundBone;

    // chain
    InRetargeterAsset.SourceIKRigAsset->RetargetDefinition.BoneChains.resize(config.SourceChains.size());
    for(size_t i = 0; i < config.SourceChains.size(); i++) {
        auto& chain = config.SourceChains[i];
        auto& BoneChain = InRetargeterAsset.SourceIKRigAsset->RetargetDefinition.BoneChains[i];

        BoneChain.ChainName = chain.chainName;
        BoneChain.StartBone.BoneName = chain.startBone;
        BoneChain.StartBone.BoneIndex = srcsk.getJointIdByName(chain.startBone);
        BoneChain.EndBone.BoneName = chain.endBone;
        BoneChain.EndBone.BoneIndex = srcsk.getJointIdByName(chain.endBone);
    }

    ///////////////////////////////////////
    // ikrig 2
    InRetargeterAsset.TargetIKRigAsset = std::make_shared<UIKRigDefinition>();

    // skeleton
    FIKRigSkeleton rigsk2;
    IKRigUtils::USkeleton2RigSkeleton(tgtusk, rigsk2);
    InRetargeterAsset.TargetIKRigAsset->Skeleton = rigsk2;

    // root bone
    InRetargeterAsset.TargetIKRigAsset->RetargetDefinition.RootBone = config.TargetRootBone;
    InRetargeterAsset.TargetIKRigAsset->RetargetDefinition.GroundBone = config.TargetGroundBone;

    // chain
    InRetargeterAsset.TargetIKRigAsset->RetargetDefinition.BoneChains.resize(config.TargetChains.size());
    for(size_t i = 0; i < config.TargetChains.size(); i++) {
        auto& chain = config.TargetChains[i];
        auto& BoneChain = InRetargeterAsset.TargetIKRigAsset->RetargetDefinition.BoneChains[i];

        BoneChain.ChainName = chain.chainName;
        BoneChain.StartBone.BoneName = chain.startBone;
        BoneChain.StartBone.BoneIndex = srcsk.getJointIdByName(chain.startBone);
        BoneChain.EndBone.BoneName = chain.endBone;
        BoneChain.EndBone.BoneIndex = srcsk.getJointIdByName(chain.endBone);
    }

    ///////////////////////////////////////
    // mapping
    for(size_t i = 0; i < config.ChainMapping.size(); i++) {
        std::shared_ptr<URetargetChainSettings> chainSetting = std::make_shared<URetargetChainSettings>();

        chainSetting->Settings.FK.EnableFK = config.ChainMapping[i].EnableFK;
        chainSetting->Settings.IK.EnableIK = config.ChainMapping[i].EnableIK;
        chainSetting->SourceChain = config.ChainMapping[i].SourceChain;
        chainSetting->TargetChain = config.ChainMapping[i].TargetChain;
        InRetargeterAsset.ChainSettings.push_back(chainSetting);
    }

    return pInRetargeterAsset;
}

static std::vector<FTransform> getMetaTPoseFPose(SoulSkeleton& sk, CoordType srcCoord, CoordType tgtCoord) {

    std::vector<FTransform> pose(sk.joints.size());

    auto soulpose = SoulIK::getMetaTPoseSoulPose(sk);
    IKRigUtils::SoulPose2FPose(soulpose, pose);
    IKRigUtils::LocalFPoseCoordConvert(srcCoord, tgtCoord, pose);

    return pose;
}

static SoulIKRigRetargetConfig config1_1chain_lleg() {
    SoulIKRigRetargetConfig config;
    config.SourceCoord  = CoordType::RightHandZupYfront;
    config.WorkCoord    = CoordType::RightHandZupYfront;
    config.TargetCoord  = CoordType::RightHandZupYfront;

    config.SourceRootBone   = "Hip";
    config.SourceGroundBone = "RightAnkle_end";
    config.TargetRootBone   = "Hip";
    config.TargetGroundBone = "RightAnkle_end";

    config.SourceChains = {
        // name    start        end
        {"lleg", "RightHip", "RightKnee"}
    };

    config.TargetChains = {
        // name    start        end
        {"lleg", "RightHip", "RightKnee"}
    };

    config.ChainMapping = {
        // fk   ik      sourceChain     targetChain
        {true,  false,  "lleg",         "lleg"}
    };

    return config;
}

static SoulIKRigRetargetConfig config2_6chain() {

    SoulIKRigRetargetConfig config;

    config.SourceCoord      = CoordType::RightHandZupYfront;
    config.WorkCoord        = CoordType::RightHandZupYfront;
    config.TargetCoord      = CoordType::RightHandZupYfront;

    config.SourceRootBone   = "Hip";
    config.SourceGroundBone = "RightAnkle_end";
    config.TargetRootBone   = "Hip";
    config.TargetGroundBone = "RightAnkle_end";

    config.SourceChains = {
        // name     start               end
        {"spine",   "Spine",            "Thorax"},
        {"lleg",    "LeftHip",          "LeftAnkle"},
        {"rleg",    "RightHip",         "RightAnkle"},
        {"larm",    "LeftShoulder",     "LeftWrist"},
        {"rram",    "RightShoulder",    "RightWrist"},
        {"head",    "Neck",             "Head"},
    };

    config.TargetChains = {
        // name     start               end
        {"spine",   "Spine",            "Thorax"},
        {"lleg",    "LeftHip",          "LeftAnkle"},
        {"rleg",    "RightHip",         "RightAnkle"},
        {"larm",    "LeftShoulder",     "LeftWrist"},
        {"rram",    "RightShoulder",    "RightWrist"},
        {"head",    "Neck",             "Head"},
    };

    config.ChainMapping = {
        // fk   ik      sourceChain     targetChain
        {true,  false,  "spine",        "spine"},
        {true,  false,  "lleg",         "lleg"},
        {true,  false,  "rleg",         "rleg"},
        {true,  false,  "larm",         "larm"},
        {true,  false,  "rram",         "rram"},
        {true,  false,  "head",         "head"},
    };

    return config;
}

static SoulIKRigRetargetConfig config_s1_meta() {
    SoulIKRigRetargetConfig config;
    config.SourceCoord      = CoordType::RightHandZupYfront;
    config.WorkCoord        = CoordType::RightHandZupYfront;
    config.TargetCoord      = CoordType::RightHandYupZfront;

    config.SourceRootBone   = "Hip";
    config.SourceGroundBone = "RightAnkle_end";
    config.TargetRootBone   = "Rol01_Torso01HipCtrlJnt_M";
    config.TargetGroundBone = "Rol01_Leg01FootJnt_L";
    
    //config.skipRootBone = true;

    config.SourceChains = {
        // name     start               end
        // spine
        {"spine",   "Spine",            "Thorax"},

        // head
        {"head",    "Neck",             "Head"},

        //{"lleg",    "LeftHip",          "LeftAnkle"},
        {"lleg1",   "LeftHip",          "LeftHip"},
        {"lleg2",   "LeftKnee",         "LeftKnee"},
        {"lleg3",   "LeftAnkle",        "LeftAnkle"},

        //{"rleg",    "RightHip",         "RightAnkle"},
        {"rleg1",   "RightHip",         "RightHip"},
        {"rleg2",   "RightKnee",        "RightKnee"},
        {"rleg3",   "RightAnkle",       "RightAnkle"},

        //{"larm",    "LeftShoulder",     "LeftWrist"},
        {"larm1",   "LeftShoulder",     "LeftShoulder"},
        {"larm2",   "LeftElbow",        "LeftElbow"},
        {"larm3",   "LeftWrist",        "LeftWrist"},
        
        //{"rram",    "RightShoulder",    "RightWrist"},
        {"rram1",   "RightShoulder",    "RightShoulder"},
        {"rram2",   "RightElbow",       "RightElbow"},
        {"rram3",   "RightWrist",       "RightWrist"},

    };

    config.TargetChains = {
        // name    start        end
        // spine
        {"spine",   "Rol01_Torso0102Jnt_M",     "Rol01_Neck0101Jnt_M"},

        // head
        {"head",    "Rol01_Neck0102Jnt_M",      "Head_M"},

        //{"lleg",    "Rol01_Leg01Up01Jnt_L",     "Rol01_Leg01AnkleJnt_L"},
        {"lleg1",   "Rol01_Leg01Up01Jnt_L",     "Rol01_Leg01Up01Jnt_L"},
        {"lleg2",   "Rol01_Leg01Low01Jnt_L",    "Rol01_Leg01Low01Jnt_L"},
        {"lleg3",   "Rol01_Leg01AnkleJnt_L",    "Rol01_Leg01AnkleJnt_L"},

        //{"rleg",    "Rol01_Leg01Up01Jnt_R",     "Rol01_Leg01AnkleJnt_R"},
        {"rleg1",   "Rol01_Leg01Up01Jnt_R",     "Rol01_Leg01Up01Jnt_R"},
        {"rleg2",   "Rol01_Leg01Low01Jnt_R",    "Rol01_Leg01Low01Jnt_R"},
        {"rleg3",   "Rol01_Leg01AnkleJnt_R",    "Rol01_Leg01AnkleJnt_R"},

        //{"larm",    "Rol01_Arm01Up01Jnt_L",     "Rol01_Arm01Low03Jnt_L"},
        {"larm1",   "Rol01_Arm01Up01Jnt_L",     "Rol01_Arm01Up01Jnt_L"},
        {"larm2",   "Rol01_Arm01Low01Jnt_L",    "Rol01_Arm01Low01Jnt_L"},
        {"larm3",   "Rol01_Hand01MasterJnt_L",  "Rol01_Hand01MasterJnt_L"},

        //{"rram",    "Rol01_Arm01Up01Jnt_R",    "Rol01_Arm01Low03Jnt_R"},
        {"rram1",   "Rol01_Arm01Up01Jnt_R",     "Rol01_Arm01Up01Jnt_R"},
        {"rram2",   "Rol01_Arm01Low01Jnt_R",    "Rol01_Arm01Low01Jnt_R"},
        {"rram3",   "Rol01_Hand01MasterJnt_R",  "Rol01_Hand01MasterJnt_R"},        
    };

    config.ChainMapping = {
        // fk   ik      sourceChain     targetChain
        
        // spine
        {true,  false,  "spine",        "spine"},

        // head
        {true,  false,  "head",         "head"},

        // lleg
        {true,  false,  "lleg1",        "lleg1"},
        {true,  false,  "lleg2",        "lleg2"},
        {true,  false,  "lleg3",        "lleg3"},
        
        // rleg
        {true,  false,  "rleg1",        "rleg1"},
        {true,  false,  "rleg2",        "rleg2"},
        {true,  false,  "rleg3",        "rleg3"},

        // larm
        {true,  false,  "larm1",        "larm1"},
        {true,  false,  "larm2",        "larm2"},
        {true,  false,  "larm3",        "larm3"},
        
        // rarm
        {true,  false,  "rram1",        "rram1"},
        {true,  false,  "rram2",        "rram2"},
        {true,  false,  "rram3",        "rram3"},
    };

    return config;
}

static SoulIKRigRetargetConfig config_flair_meta() {
    SoulIKRigRetargetConfig config;
    config.SourceCoord      = CoordType::RightHandZupYfront;
    config.WorkCoord        = CoordType::RightHandZupYfront;
    config.TargetCoord      = CoordType::RightHandYupZfront;

    config.SourceRootBone   = "mixamorig:Hips";
    config.SourceGroundBone = "mixamorig:LeftToeBase";
    config.TargetRootBone   = "Rol01_Torso01HipCtrlJnt_M";
    config.TargetGroundBone = "Rol01_Leg01FootJnt_L";
    
    //config.skipRootBone = true;

    config.SourceChains = {
        // name     start                               end
        // spine
        {"spine",       "mixamorig:Spine",              "mixamorig:Spine2"},

        // head
        {"head",        "mixamorig:Neck",               "mixamorig:Head"},

        // lleg
        {"lleg1",       "mixamorig:LeftUpLeg",          "mixamorig:LeftUpLeg"},
        {"lleg2",       "mixamorig:LeftLeg",            "mixamorig:LeftLeg"},
        {"lleg3",       "mixamorig:LeftFoot",           "mixamorig:LeftFoot"},

        // rleg
        {"rleg1",       "mixamorig:RightUpLeg",         "mixamorig:RightUpLeg"},
        {"rleg2",       "mixamorig:RightLeg",           "mixamorig:RightLeg"},
        {"rleg3",       "mixamorig:RightFoot",          "mixamorig:RightFoot"},

        // larm
        {"larm0",       "mixamorig:LeftShoulder",       "mixamorig:LeftShoulder"},
        {"larm1",       "mixamorig:LeftArm",            "mixamorig:LeftArm"},
        {"larm2",       "mixamorig:LeftForeArm",        "mixamorig:LeftForeArm"},
        {"larm3",       "mixamorig:LeftHand",           "mixamorig:LeftHand"},
        
        // lhand
        {"lfinger1",    "mixamorig:LeftHandThumb1",     "mixamorig:LeftHandThumb3"},
        {"lfinger2",    "mixamorig:LeftHandIndex1",     "mixamorig:LeftHandIndex3"},
        {"lfinger3",    "mixamorig:LeftHandMiddle1",    "mixamorig:LeftHandMiddle3"},
        {"lfinger4",    "mixamorig:LeftHandRing1",      "mixamorig:LeftHandRing3"},
        {"lfinger5",    "mixamorig:LeftHandPinky1",     "mixamorig:LeftHandPinky3"},

        // rarm
        {"rram0",       "mixamorig:RightShoulder",      "mixamorig:RightShoulder"},
        {"rram1",       "mixamorig:RightArm",           "mixamorig:RightArm"},
        {"rram2",       "mixamorig:RightForeArm",       "mixamorig:RightForeArm"},
        {"rram3",       "mixamorig:RightHand",          "mixamorig:RightHand"},

        // rhand
        {"rfinger1",    "mixamorig:RightHandThumb1",    "mixamorig:RightHandThumb3"},
        {"rfinger2",    "mixamorig:RightHandIndex1",    "mixamorig:RightHandIndex3"},
        {"rfinger3",    "mixamorig:RightHandMiddle1",   "mixamorig:RightHandMiddle3"},
        {"rfinger4",    "mixamorig:RightHandRing1",     "mixamorig:RightHandRing3"},
        {"rfinger5",    "mixamorig:RightHandPinky1",    "mixamorig:RightHandPinky3"},
    };

    config.TargetChains = {
        // name     start                           end
        // spine
        {"spine",       "Rol01_Torso0102Jnt_M",             "Rol01_Neck0101Jnt_M"},

        // head
        {"head",        "Rol01_Neck0102Jnt_M",              "Head_M"},

        // lleg
        {"lleg1",       "Rol01_Leg01Up01Jnt_L",             "Rol01_Leg01Up01Jnt_L"},
        {"lleg2",       "Rol01_Leg01Low01Jnt_L",            "Rol01_Leg01Low01Jnt_L"},
        {"lleg3",       "Rol01_Leg01AnkleJnt_L",            "Rol01_Leg01AnkleJnt_L"},

        // rleg
        {"rleg1",       "Rol01_Leg01Up01Jnt_R",             "Rol01_Leg01Up01Jnt_R"},
        {"rleg2",       "Rol01_Leg01Low01Jnt_R",            "Rol01_Leg01Low01Jnt_R"},
        {"rleg3",       "Rol01_Leg01AnkleJnt_R",            "Rol01_Leg01AnkleJnt_R"},

        // larm
        {"larm0",       "Rol01_Arm01ClavicleStartJnt_L",    "Rol01_Arm01ClavicleStartJnt_L"},
        {"larm1",       "Rol01_Arm01Up01Jnt_L",             "Rol01_Arm01Up01Jnt_L"},
        {"larm2",       "Rol01_Arm01Low01Jnt_L",            "Rol01_Arm01Low01Jnt_L"},
        {"larm3",       "Rol01_Hand01MasterJnt_L",          "Rol01_Hand01MasterJnt_L"},

        // lhand
        {"lfinger1",    "Rol01_Hand01Thumb01FKCtrlJnt_L",   "Rol01_Hand01Thumb03FKCtrlJnt_L"},
        {"lfinger2",    "Rol01_Hand01Index02FKCtrlJnt_L",   "Rol01_Hand01Index04FKCtrlJnt_L"},
        {"lfinger3",    "Rol01_Hand01Middle02FKCtrlJnt_L",  "Rol01_Hand01Middle04FKCtrlJnt_L"},
        {"lfinger4",    "Rol01_Hand01Ring02FKCtrlJnt_L",    "Rol01_Hand01Ring04FKCtrlJnt_L"},
        {"lfinger5",    "Rol01_Hand01Little02FKCtrlJnt_L",  "Rol01_Hand01Little04FKCtrlJnt_L"},

        // rarm
        {"rram0",       "Rol01_Arm01ClavicleStartJnt_R",    "Rol01_Arm01ClavicleStartJnt_R"},
        {"rram1",       "Rol01_Arm01Up01Jnt_R",             "Rol01_Arm01Up01Jnt_R"},
        {"rram2",       "Rol01_Arm01Low01Jnt_R",            "Rol01_Arm01Low01Jnt_R"},
        {"rram3",       "Rol01_Hand01MasterJnt_R",          "Rol01_Hand01MasterJnt_R"},

        // rhand
        {"rfinger1",    "Rol01_Hand01Thumb01FKCtrlJnt_R",   "Rol01_Hand01Thumb03FKCtrlJnt_R"},
        {"rfinger2",    "Rol01_Hand01Index02FKCtrlJnt_R",   "Rol01_Hand01Index04FKCtrlJnt_R"},
        {"rfinger3",    "Rol01_Hand01Middle02FKCtrlJnt_R",  "Rol01_Hand01Middle04FKCtrlJnt_R"},
        {"rfinger4",    "Rol01_Hand01Ring02FKCtrlJnt_R",    "Rol01_Hand01Ring04FKCtrlJnt_R"},
        {"rfinger5",    "Rol01_Hand01Little02FKCtrlJnt_R",  "Rol01_Hand01Little04FKCtrlJnt_R"},
    };

    config.ChainMapping = {
        // fk   ik      sourceChain     targetChain
        
        // spine
        {true,  false,  "spine",        "spine"},

        // head
        {true,  false,  "head",         "head"},

        // lleg
        {true,  false,  "lleg1",        "lleg1"},
        {true,  false,  "lleg2",        "lleg2"},
        {true,  false,  "lleg3",        "lleg3"},
        
        // rleg
        {true,  false,  "rleg1",        "rleg1"},
        {true,  false,  "rleg2",        "rleg2"},
        {true,  false,  "rleg3",        "rleg3"},

        // larm
        {true,  false,  "larm0",        "larm0"},
        {true,  false,  "larm1",        "larm1"},
        {true,  false,  "larm2",        "larm2"},
        {true,  false,  "larm3",        "larm3"},

        // lhand
        {true,  false,  "lfinger1",     "lfinger1"},
        {true,  false,  "lfinger2",     "lfinger2"},
        {true,  false,  "lfinger3",     "lfinger3"},
        {true,  false,  "lfinger4",     "lfinger4"},
        {true,  false,  "lfinger5",     "lfinger5"},
        
        // rarm
        {true,  false,  "rram0",        "rram0"},
        {true,  false,  "rram1",        "rram1"},
        {true,  false,  "rram2",        "rram2"},
        {true,  false,  "rram3",        "rram3"},

        // rhand
        {true,  false,  "rfinger1",     "rfinger1"},
        {true,  false,  "rfinger2",     "rfinger2"},
        {true,  false,  "rfinger3",     "rfinger3"},
        {true,  false,  "rfinger4",     "rfinger4"},
        {true,  false,  "rfinger5",     "rfinger5"},
    };

    return config;
}

static std::string getModelPath() {
    std::string file_path = __FILE__;
    
    #ifdef _WIN64
        std::string dir_path = file_path.substr(0, file_path.rfind("\\"));
        std::string model_path = dir_path + "\\..\\..\\model\\";
    #else
        std::string dir_path = file_path.substr(0, file_path.rfind("/"));
        std::string model_path = dir_path + "/../../model/";
    #endif

    return model_path;
}

static void getFilePaths(std::string& srcAnimationFile, 
    std::string& srcTPoseFile, 
    std::string& targetFile,
    std::string& outfile, 
    SoulIKRigRetargetConfig& config) {
    
    std::string modelPath = getModelPath();

    srcAnimationFile    = modelPath + "S1_SittingDown_3d_17kpts.fbx";
    srcTPoseFile        = modelPath + "S1_SittingDown_3d_17kpts.fbx";
    targetFile          = modelPath + "3D_Avatar2_Rig_0723_itpose.fbx";
    outfile             = modelPath + "out.fbx";

    if (config.TargetCoord == CoordType::RightHandZupYfront) {
        targetFile      = modelPath = "S1_SittingDown_3d_17kpts_tiny_ball";
    }
}

int main(int argc, char *argv[]) {

    /////////////////////////////////////////////
    // setting of coord
    //auto config             =  config1_1chain_lleg();
    auto config             =  config_s1_meta();
    //auto config             = config2_6chain();
    //auto config             = config_flair_meta();
    CoordType srccoord      = config.SourceCoord;
    CoordType workcoord     = config.WorkCoord;
    CoordType tgtcoord      = config.TargetCoord;
    FTransform tsrc2work    = IKRigUtils::getFTransformFromCoord(srccoord, workcoord);
    FTransform twork2tgt    = IKRigUtils::getFTransformFromCoord(workcoord, tgtcoord);
    FTransform ttgt2work    = IKRigUtils::getFTransformFromCoord(tgtcoord, workcoord);

    /////////////////////////////////////////////
    // read fbx
    std::string srcAnimationFile, srcTPoseFile, targetFile, outfile;
    getFilePaths(srcAnimationFile, srcTPoseFile, targetFile, outfile, config);

    SoulIK::FBXRW fbxSrcAnimation, fbxSrcTPose, fbxTarget;
    fbxSrcAnimation.readPureSkeletonWithDefualtMesh(srcAnimationFile, "Hip");
    fbxSrcTPose.readPureSkeletonWithDefualtMesh(srcTPoseFile, "Hip");
    fbxTarget.readSkeletonMesh(targetFile);

    SoulIK::SoulScene& srcscene         = *fbxSrcAnimation.getSoulScene();
    SoulIK::SoulScene& srcTPoseScene    = *fbxSrcTPose.getSoulScene();
    SoulIK::SoulScene& tgtscene         = *fbxTarget.getSoulScene();
    SoulIK::SoulSkeletonMesh& srcskm    = *srcscene.skmeshes[0];
    SoulIK::SoulSkeletonMesh& tgtskm    = *tgtscene.skmeshes[0];

    #ifdef DEBUG_POSE_PRINT
    IKRigUtils::debugPrintSKM(srcTPoseScene, *srcTPoseScene.skmeshes[0], srccoord, workcoord);
    IKRigUtils::debugPrintSKM(tgtscene,tgtskm, tgtcoord, workcoord);
    #endif

    /////////////////////////////////////////////
    // init
    SoulIK::USkeleton srcusk;
    SoulIK::USkeleton tgtusk;
    IKRigUtils::getUSkeletonFromMesh(srcTPoseScene, *srcTPoseScene.skmeshes[0], srcusk, srccoord, workcoord);
    IKRigUtils::getUSkeletonFromMesh(tgtscene, tgtskm, tgtusk, tgtcoord, workcoord);
    //tgtusk.refpose = getMetaTPoseFPose(tgtskm.skeleton, CoordType::RightHandYupZfront, CoordType::RightHandZupYfront);

    #ifdef DEBUG_POSE_PRINT
    IKRigUtils::debugPrintUSK(srcusk, srcskm, srccoord, workcoord);
    IKRigUtils::debugPrintUSK(tgtusk, tgtskm, tgtcoord, workcoord);
    #endif

    SoulIK::UIKRetargetProcessor ikretarget;
	std::shared_ptr<UIKRetargeter> InRetargeterAsset = createIKRigAsset(config, srcskm.skeleton, tgtskm.skeleton, srcusk, tgtusk);
    ikretarget.Initialize(&srcusk, &tgtusk, InRetargeterAsset.get(), false); // todo: raw pointer not safe
    
    /////////////////////////////////////////////
    // build pose animation form mesh0
    std::vector<SoulIK::SoulPose> tempposes;
    std::vector<SoulIK::SoulPose> tempoutposes;
    buildPoseAnimationByInterpolation(srcscene, srcskm, srcskm.animation, tempposes);
    int frameCount = tempposes.size();
    tempoutposes.resize(tempposes.size());

    /////////////////////////////////////////////
    // run retarget
    std::unordered_map<FName, float> SpeedValuesFromCurves;
    float DeltaTime = 0;

    std::vector<FTransform> inpose;
    std::vector<FTransform> inposeLocal;
    std::vector<FTransform> outposeLocal;
    for(int frame = 0; frame < frameCount; frame++) {

        // input and cast
        IKRigUtils::SoulPose2FPose(tempposes[frame], inposeLocal);

        // coord convert
        IKRigUtils::LocalFPoseCoordConvert(tsrc2work, srccoord, workcoord, inposeLocal);

        // to global
        IKRigUtils::FPoseToGlobal(srcskm.skeleton, inposeLocal, inpose);

        #ifdef DEBUG_POSE_PRINT
        printf("frame:%d\n", frame);
        IKRigUtils::debugPrintInputSoulPose(tempposes[frame], srcskm, frame);
        IKRigUtils::debugPrintIOFPose("in", srcskm, inposeLocal, inpose, frame);
        #endif

        // retarget
        std::vector<FTransform>& outpose = ikretarget.RunRetargeter(inpose, SpeedValuesFromCurves, DeltaTime);

        // to local
        IKRigUtils::FPoseToLocal(tgtskm.skeleton, outpose, outposeLocal);

        #ifdef DEBUG_POSE_PRINT
        IKRigUtils::debugPrintIOFPose("out", tgtskm, outposeLocal, outpose, frame);
        #endif

        // coord convert
        IKRigUtils::LocalFPoseCoordConvert(twork2tgt, workcoord, tgtcoord, outposeLocal);

        // cast and output
        IKRigUtils::FPose2SoulPose(outposeLocal, tempoutposes[frame]);

        // test init fpose
        // std::vector<FTransform> initposeLocal2 = tgtusk.refpose;
        // IKRigUtils::LocalPoseCoordConvert(twork2tgt, initposeLocal2, workcoord, tgtcoord);
        // IKRigUtils::FPose2SoulPose(initposeLocal2, tempoutposes[frame]);
        
        // test init soulpose
        //SoulIK::SoulPose tgtsoulpose;
        //IKRigUtils::getSoulPoseFromMesh(tgtscene, tgtskm, tgtsoulpose);
        //tempoutposes[frame] = tgtsoulpose;

        // test inpose
        //tempoutposes[frame] = tempposes[frame];
    }

    printf("process animation %d keyframes\n", frameCount);

    /////////////////////////////////////////////
    // output pose animation to mesh0
    writePoseAnimationToMesh(tempoutposes, tgtskm, frameCount, srcskm.animation.ticksPerSecond);
    fbxTarget.writeSkeletonMesh(outfile);

    return 0;
}
