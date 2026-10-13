#include <cstddef>
extern "C" {
#include "pair_ap/pair-tlv.h"
}
#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <vector>
#include <type_traits>

TEST(PairTlv, EmptyCollectionNeedsNoOutputStorage) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  size_t size = 0;

  EXPECT_EQ(pair_tlv_format(values.get(), nullptr, &size), 0);
  EXPECT_EQ(size, 0);
}

TEST(PairTlv, EmptyValueRequiresHeaderBeforeWritingOutput) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_Separator, nullptr, 0), 0);
  std::array<uint8_t, 2> output{0x5a, 0xa5};
  size_t capacity = 0;

  EXPECT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), PAIR_TLV_ERROR_INSUFFICIENT_SIZE);
  EXPECT_EQ(capacity, 2);
  EXPECT_EQ(output, (std::array<uint8_t, 2>{0x5a, 0xa5}));
}

TEST(PairTlv, EmptyValueRejectsOneByteCapacityAndWritesExactTwoByteHeader) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_FragmentData, nullptr, 0), 0);
  std::array<uint8_t, 3> output{0x5a, 0xa5, 0x3c};
  size_t capacity = 1;
  EXPECT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), PAIR_TLV_ERROR_INSUFFICIENT_SIZE);
  EXPECT_EQ(capacity, 2);
  EXPECT_EQ(output, (std::array<uint8_t, 3>{0x5a, 0xa5, 0x3c}));

  ASSERT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), 0);

  EXPECT_EQ(capacity, 2);
  EXPECT_EQ(output, (std::array<uint8_t, 3>{TLVType_FragmentData, 0, 0x3c}));
}

TEST(PairTlv, MixedValuesIncludeEmptySeparatorInRequiredSizeAndWireBytes) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  const uint8_t state = 2;
  const std::array<uint8_t, 2> key{0x12, 0x34};
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_State, &state, 1), 0);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_Separator, nullptr, 0), 0);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_PublicKey, key.data(), key.size()), 0);
  size_t capacity = 0;
  EXPECT_EQ(pair_tlv_format(values.get(), nullptr, &capacity), PAIR_TLV_ERROR_INSUFFICIENT_SIZE);
  ASSERT_EQ(capacity, 9);
  std::array<uint8_t, 9> output{};

  ASSERT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), 0);

  EXPECT_EQ(capacity, output.size());
  EXPECT_EQ(output, (std::array<uint8_t, 9>{6, 1, 2, 255, 0, 3, 2, 0x12, 0x34}));
}

TEST(PairTlv, ValueBeyond255BytesUsesTwoFragmentsAndParsesAsOneValue) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  std::vector<uint8_t> payload(255, 0x41);
  payload.push_back(0x42);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_PublicKey, payload.data(), payload.size()), 0);
  std::vector<uint8_t> expected{3, 255};
  expected.insert(expected.end(), 255, 0x41);
  expected.insert(expected.end(), {3, 1, 0x42});
  std::array<uint8_t, 260> output{};
  size_t capacity = output.size();

  ASSERT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), 0);

  EXPECT_EQ(capacity, 260);
  EXPECT_EQ(std::vector<uint8_t>(output.begin(), output.end()), expected);
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> parsed(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(parsed);
  ASSERT_EQ(pair_tlv_parse(output.data(), output.size(), parsed.get()), 0);
  const auto *key = pair_tlv_get_value(parsed.get(), TLVType_PublicKey);
  ASSERT_NE(key, nullptr);
  EXPECT_EQ(key->size, payload.size());
  EXPECT_EQ(std::vector<uint8_t>(key->value, key->value + key->size), payload);
  EXPECT_EQ(key->next, nullptr);
}

TEST(PairTlv, ParsingEmptyValuePreservesItsTypeAndZeroLength) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  const std::array<uint8_t, 2> input{TLVType_Separator, 0};

  ASSERT_EQ(pair_tlv_parse(input.data(), input.size(), values.get()), 0);

  const auto *separator = pair_tlv_get_value(values.get(), TLVType_Separator);
  ASSERT_NE(separator, nullptr);
  EXPECT_EQ(separator->size, 0);
  EXPECT_EQ(separator->value, nullptr);
  EXPECT_EQ(separator->next, nullptr);
}

TEST(PairTlv, TruncatedHeaderAndPayloadFailWithoutAddingAValue) {
  for (const auto &input : {std::vector<uint8_t>{6}, std::vector<uint8_t>{6, 2, 1}}) {
    SCOPED_TRACE(testing::PrintToString(input));
    std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
    ASSERT_TRUE(values);

    EXPECT_EQ(pair_tlv_parse(input.data(), input.size(), values.get()), PAIR_TLV_ERROR_INSUFFICIENT_SIZE);

    EXPECT_EQ(values->head, nullptr);
  }
}

TEST(PairTlv, MalformedLaterValueRetainsEarlierValuesAndBorrowedPointers) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  const uint8_t method = 0;
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_Method, &method, 1), 0);
  const auto *borrowed = pair_tlv_get_value(values.get(), TLVType_Method);
  const std::array<uint8_t, 6> input{TLVType_State, 1, 2, TLVType_PublicKey, 2, 0x41};

  EXPECT_EQ(pair_tlv_parse(input.data(), input.size(), values.get()), PAIR_TLV_ERROR_INSUFFICIENT_SIZE);

  EXPECT_EQ(pair_tlv_get_value(values.get(), TLVType_Method), borrowed);
  ASSERT_NE(borrowed->next, nullptr);
  EXPECT_EQ(borrowed->next->type, TLVType_State);
  EXPECT_EQ(borrowed->next->value[0], 2);
  EXPECT_EQ(borrowed->next->next, nullptr);
  EXPECT_EQ(pair_tlv_get_value(values.get(), TLVType_PublicKey), nullptr);
}

TEST(PairTlv, RepeatedShortValuesRemainSeparateAndLookupReturnsFirst) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  const std::array<uint8_t, 6> input{TLVType_State, 1, 2, TLVType_State, 1, 4};

  ASSERT_EQ(pair_tlv_parse(input.data(), input.size(), values.get()), 0);

  const auto *first = pair_tlv_get_value(values.get(), TLVType_State);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->size, 1);
  EXPECT_EQ(first->value[0], 2);
  ASSERT_NE(first->next, nullptr);
  EXPECT_EQ(first->next->type, TLVType_State);
  EXPECT_EQ(first->next->value[0], 4);
  EXPECT_EQ(first->next->next, nullptr);
  std::array<uint8_t, 6> output{};
  size_t capacity = output.size();
  EXPECT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), 0);
  EXPECT_EQ(output, input);
}

TEST(PairTlv, Exactly255BytesFormatsAsOneFragmentAndCopiesInput) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  std::array<uint8_t, 255> input{};
  input.fill(0x41);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_PublicKey, input.data(), input.size()), 0);
  input.fill(0x42);
  std::array<uint8_t, 257> output{};
  size_t capacity = output.size();

  ASSERT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), 0);

  EXPECT_EQ(capacity, output.size());
  EXPECT_EQ(output[0], TLVType_PublicKey);
  EXPECT_EQ(output[1], 255);
  EXPECT_EQ(output[2], 0x41);
  EXPECT_EQ(output.back(), 0x41);
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> parsed(pair_tlv_new(), pair_tlv_free);
  ASSERT_EQ(pair_tlv_parse(output.data(), output.size(), parsed.get()), 0);
  const auto *key = pair_tlv_get_value(parsed.get(), TLVType_PublicKey);
  ASSERT_NE(key, nullptr);
  EXPECT_EQ(key->size, 255);
  EXPECT_EQ(key->next, nullptr);
}

TEST(PairTlv, ScopedOwnerExposesTheSameWireBehaviorThroughTheCFacade) {
  pair_tlv_values_t message;
  const uint8_t state = 2;
  ASSERT_EQ(message.add(TLVType_State, &state, 1), 0);
  std::array<uint8_t, 3> wire{};
  size_t capacity = wire.size();
  ASSERT_EQ(message.format(wire.data(), &capacity), 0);
  EXPECT_EQ(wire, (std::array<uint8_t, 3>{TLVType_State, 1, 2}));
  EXPECT_EQ(pair_tlv_get_value(&message, TLVType_State), message.find(TLVType_State));

  pair_tlv_values_t parsed;
  ASSERT_EQ(parsed.parse(wire.data(), wire.size()), 0);
  const auto *value = parsed.find(TLVType_State);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->value[0], state);
  static_assert(!std::is_copy_constructible_v<pair_tlv_values_t>);
  static_assert(!std::is_move_constructible_v<pair_tlv_values_t>);
  static_assert(std::is_standard_layout_v<pair_tlv_values_t>);
  static_assert(sizeof(pair_tlv_values_t) == sizeof(pair_tlv_t *));
}
