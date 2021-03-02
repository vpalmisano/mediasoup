#include "common.hpp"
#include "Utils.hpp"
#include <catch.hpp>

using namespace Utils;

SCENARIO("Utils::Bits::CountSetBits()")
{
	uint16_t mask;

	mask = 0b0000000000000000;
	REQUIRE(Utils::Bits::CountSetBits(mask) == 0);

	mask = 0b0000000000000001;
	REQUIRE(Utils::Bits::CountSetBits(mask) == 1);

	mask = 0b1000000000000001;
	REQUIRE(Utils::Bits::CountSetBits(mask) == 2);

	mask = 0b1111111111111111;
	REQUIRE(Utils::Bits::CountSetBits(mask) == 16);
}

SCENARIO("Utils::Bits::ReadBits()")
{
	uint8_t data[] = {
		0b10110000,
		0b00001111,
		0b11110011,
		0b00001011,
		0b11001111
	};
	uint32_t bitOffset = 0;

	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 1, bitOffset) == 0b1);
	REQUIRE(bitOffset == 1);
	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 2, bitOffset) == 0b01);
	REQUIRE(bitOffset == 3);
	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 2, bitOffset) == 0b10);
	REQUIRE(bitOffset == 5);
	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 3, bitOffset) == 0b000);
	REQUIRE(bitOffset == 8);

	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 5, bitOffset) == 0b00001);
	REQUIRE(bitOffset == 13);
	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 7, bitOffset) == 0b1111111);
	REQUIRE(bitOffset == 20);
	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 15, bitOffset) == 0b001100001011110);
	REQUIRE(bitOffset == 35);
	// check reading out of bounds
	REQUIRE(Utils::Bits::ReadBits(data, sizeof(data), 8, bitOffset) == 0b1111);
	REQUIRE(bitOffset == 40);
}

SCENARIO("Utils::Bits::ReadBitsNonSymmetric()")
{
	uint8_t data[] = {
		0b00011011,
		0b01110000
	};
	uint32_t bitOffset = 0;

	REQUIRE(Utils::Bits::ReadBitsNonSymmetric(data, sizeof(data), 5, bitOffset) == 0b000);
	REQUIRE(bitOffset == 2);
	REQUIRE(Utils::Bits::ReadBitsNonSymmetric(data, sizeof(data), 5, bitOffset) == 0b001);
	REQUIRE(bitOffset == 4);
	REQUIRE(Utils::Bits::ReadBitsNonSymmetric(data, sizeof(data), 5, bitOffset) == 0b010);
	REQUIRE(bitOffset == 6);
	REQUIRE(Utils::Bits::ReadBitsNonSymmetric(data, sizeof(data), 5, bitOffset) == 0b011);
	REQUIRE(bitOffset == 9);
	REQUIRE(Utils::Bits::ReadBitsNonSymmetric(data, sizeof(data), 5, bitOffset) == 0b100);
	REQUIRE(bitOffset == 12);
}
