/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#include <vector>
#include <memory>
#include "ByteTrack/BYTETracker.h"
#include "detection_types.h"

static std::unique_ptr<byte_track::BYTETracker> tracker;

// static void box_to_rect(const BOX_RECT& box, byte_track::Rect<float>& rect)
// {
//     rect.x() = (float)(box.left);
//     rect.y() = (float)(box.top);
//     rect.width() = (float)(abs(box.right - box.left));
//     rect.height() = (float)(abs(box.top - box.bottom));
// }

static void rect_to_box(detection_box_t& box, const byte_track::Rect<float>& rect)
{
    box.left = (int)rect.x();
    box.top = (int)rect.y();
    box.bottom = (int)rect.height() + (int)rect.y();
    box.right = (int)rect.width() + (int)rect.x();
}

static void stracks_to_detected_results(std::vector<byte_track::BYTETracker::STrackPtr>& stracks,
                                        detection_result_group_t* detected_res)
{
    for (size_t i = 0; i < stracks.size(); i++) {
        rect_to_box(detected_res->results[i].box, stracks[i]->getRect());
        detected_res->results[i].track_id = stracks[i]->getTrackId();
    }
    detected_res->count = stracks.size();
}

static void detected_results_to_objects(std::vector<byte_track::Object>& objects, detection_result_group_t* input)
{
    for (int i = 0; i < input->count; i++) {
        const byte_track::Object obj{ {
                                          (float)(input->results[i].box.left),
                                          (float)(input->results[i].box.top),
                                          (float)(abs(input->results[i].box.right - input->results[i].box.left)),
                                          (float)(abs(input->results[i].box.top - input->results[i].box.bottom)),
                                      },
                                      input->results[i].obj_class,
                                      input->results[i].confidence / 100.0F };
        objects.push_back(obj);
    }
}

extern "C" int bytetrack_init(int frame_rate, int track_buffer)
{
    tracker = std::make_unique<byte_track::BYTETracker>(frame_rate, track_buffer);
    return tracker != nullptr;
}

extern "C" int bytetrack_update(detection_result_group_t* input)
{
    std::vector<byte_track::Object> objects;
    detected_results_to_objects(objects, input);
    std::vector<byte_track::BYTETracker::STrackPtr> output_stracks = tracker->update(objects);
    stracks_to_detected_results(output_stracks, input);
    objects.clear();
    return 0;
}
