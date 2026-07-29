import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
KERNEL = ROOT / "src/ep/kernels/tilexr_ep_urma_combine_kernel.cpp"
BUILDER = ROOT / "tests/ep/demo/build_tilexr_ep_urma_combine_variant.sh"
VARIANT_BUILDER = (
    ROOT / "tests/ep/demo/"
    "build_tilexr_ep_urma_combine_p44_s20_qdc_v3_balanced_profile_variant.sh"
)


def route_owners(total_routes, pack_lanes, send_lanes):
    owners = {}
    sender_positions = [[] for _ in range(send_lanes)]
    for lane in range(pack_lanes):
        begin = total_routes * lane // pack_lanes
        end = total_routes * (lane + 1) // pack_lanes
        lane_length = end - begin
        remainder = lane_length % send_lanes
        reverse = lane % 2 != 0
        rotation = lane % send_lanes
        if remainder:
            rotation = begin % send_lanes
            if reverse:
                rotation = (rotation + remainder - 1) % send_lanes
        for sender in range(send_lanes):
            signed_delta = rotation - sender if reverse else sender - rotation
            delta = (signed_delta + send_lanes) % send_lanes
            for route in range(begin + delta, end, send_lanes):
                if route in owners:
                    raise AssertionError(f"route {route} assigned twice")
                owners[route] = sender
                sender_positions[sender].append((lane, route - begin, lane_length))
    return owners, sender_positions


class SendRouteBalanceTest(unittest.TestCase):
    def assert_balanced_coverage(self, total_routes, pack_lanes, send_lanes):
        owners, positions = route_owners(total_routes, pack_lanes, send_lanes)
        self.assertEqual(set(owners), set(range(total_routes)))
        counts = [len(sender_positions) for sender_positions in positions]
        self.assertLessEqual(max(counts, default=0) - min(counts, default=0), 1)
        return counts, positions

    def test_p44_s20_bs32_and_bs128_are_exactly_balanced(self):
        counts32, _ = self.assert_balanced_coverage(192, 44, 20)
        counts128, _ = self.assert_balanced_coverage(768, 44, 20)
        self.assertEqual(counts32, [10] * 12 + [9] * 8)
        self.assertEqual(counts128, [39] * 8 + [38] * 12)

    def test_original_pathology_is_removed_for_p48_s16_bs128(self):
        counts, positions = self.assert_balanced_coverage(768, 48, 16)
        self.assertEqual(counts, [48] * 16)
        for sender_positions in positions:
            offset_counts = [0] * 16
            for _, offset, lane_length in sender_positions:
                self.assertEqual(lane_length, 16)
                offset_counts[offset] += 1
            self.assertEqual(offset_counts, [3] * 16)

    def test_property_coverage_and_balance(self):
        for total_routes in range(65):
            for pack_lanes in (1, 2, 3, 7, 20, 44, 48, 64):
                for send_lanes in (1, 2, 3, 5, 16, 20):
                    with self.subTest(
                        total_routes=total_routes,
                        pack_lanes=pack_lanes,
                        send_lanes=send_lanes,
                    ):
                        self.assert_balanced_coverage(total_routes, pack_lanes, send_lanes)

    def test_kernel_and_variant_encode_balanced_p44_s20(self):
        kernel = KERNEL.read_text(encoding="utf-8")
        builder = BUILDER.read_text(encoding="utf-8")
        variant = VARIANT_BUILDER.read_text(encoding="utf-8")
        self.assertIn("const bool reverse = (lane & 1) != 0;", kernel)
        self.assertNotIn(
            "senderId - begin % TileXREp::kEpUrmaCombineSendLaneCount", kernel
        )
        self.assertIn('"TILEXR_VARIANT_SEND_ROUTE_BALANCED=1"', builder)
        self.assertIn(
            'EXPECTED_VARIANT="s1-p44-s20-db1-prp20-sgwin-qdc-v3-txb1-srb-profile-v1"',
            variant,
        )
        self.assertIn('"${EXPECTED_VARIANT}" 20 1 1', variant)


if __name__ == "__main__":
    unittest.main()
