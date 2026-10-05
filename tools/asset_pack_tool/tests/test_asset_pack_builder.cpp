#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

#include "nau/asset_pack/asset_pack_builder.h"
#include "nau/io/asset_pack_file_system.h"
#include "nau/io/memory_stream.h"
#include "nau/rtti/rtti_impl.h"
#include "nau/runtime/internal/runtime_state.h"

namespace
{
    class FailedWriter : public nau::io::IStreamWriter
    {
        NAU_CLASS_(FailedWriter, nau::io::IStreamWriter)
    public:
        explicit FailedWriter(bool shortWrite) :
            m_short(shortWrite)
        {
        }
        size_t getPosition() const override
        {
            return 0;
        }
        size_t setPosition(nau::io::OffsetOrigin, int64_t) override
        {
            return 0;
        }
        void flush() override
        {
        }
        nau::Result<size_t> write(const std::byte*, size_t count) override
        {
            if (m_short)
                return count ? count - 1 : 0;
            return NauMakeError("injected write failure");
        }
        bool m_short;
    };
    class FailedReader : public nau::io::IStreamReader
    {
        NAU_CLASS_(FailedReader, nau::io::IStreamReader)
    public:
        size_t getPosition() const override
        {
            return 0;
        }
        size_t setPosition(nau::io::OffsetOrigin, int64_t) override
        {
            return 0;
        }
        nau::Result<size_t> read(std::byte*, size_t) override
        {
            return NauMakeError("injected read failure");
        }
    };
}  // namespace

TEST(AssetPackBuilder, PropagatesFailures)
{
    auto output = nau::io::createMemoryStream();
    std::vector<nau::PackInputFileData> missing;
    missing.push_back({[]() -> nau::io::IStreamReader::Ptr
    {
        return nullptr;
    }, "a"});
    EXPECT_FALSE(nau::buildAssetPackage(missing, {}, output));
    std::vector<nau::PackInputFileData> failed;
    failed.push_back({[]() -> nau::io::IStreamReader::Ptr
    {
        return nau::rtti::createInstance<FailedReader>();
    }, "a"});
    EXPECT_FALSE(nau::buildAssetPackage(failed, {}, output));
    for (bool shortWrite : {true, false})
        EXPECT_FALSE(nau::buildAssetPackage({}, {}, nau::rtti::createInstance<FailedWriter>(shortWrite)));
    EXPECT_FALSE(nau::readAssetPackage(nullptr));
}

TEST(AssetPackBuilder, RoundTrip)
{
    const std::string bytes = "abc";
    nau::PackInputFileData input{[bytes]() -> nau::io::IStreamReader::Ptr
    {
        auto stream = nau::io::createMemoryStream();
        stream->write(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()).ignore();
        stream->setPosition(nau::io::OffsetOrigin::Begin, 0);
        return stream;
    }, "nested/a"};
    auto output = nau::io::createMemoryStream();
    std::vector<nau::PackInputFileData> inputs;
    inputs.push_back(std::move(input));
    ASSERT_TRUE(nau::buildAssetPackage(inputs, {}, output));
    auto index = nau::readAssetPackage(output);
    ASSERT_TRUE(index);
    EXPECT_EQ(index->content.front().contentCompression, "");
    auto fs = nau::io::createAssetPackFileSystem(output, output->getBufferAsSpan().size());
    ASSERT_TRUE(fs);
    auto file = (*fs)->openFile("nested/a", nau::io::AccessMode::Read, nau::io::OpenFileMode::OpenExisting);
    ASSERT_TRUE(file);
    nau::io::IStreamReader::Ptr stream = file->createStream();
    char buffer[3];
    ASSERT_TRUE(stream->read(reinterpret_cast<std::byte*>(buffer), 3));
    EXPECT_EQ(std::string(buffer, 3), bytes);
}

#ifdef _WIN32
TEST(AssetPackBuilder, ExistingWindowsReaderCompatibility)
{
    auto runtime = nau::RuntimeState::create();
    const std::string path = "writer-compatibility.nau_pack";
    const std::string payload(70000, 'x');
    std::vector<nau::PackInputFileData> inputs;
    inputs.push_back({[payload]() -> nau::io::IStreamReader::Ptr
    {
        auto memory = nau::io::createMemoryStream();
        memory->write(reinterpret_cast<const std::byte*>(payload.data()), payload.size()).ignore();
        memory->setPosition(nau::io::OffsetOrigin::Begin, 0);
        return memory;
    }, "nested/data"});
    nau::io::IStreamWriter::Ptr output = nau::io::createNativeFileStream(path.c_str(), nau::io::AccessMode::Write, nau::io::OpenFileMode::CreateAlways);
    ASSERT_TRUE(nau::buildAssetPackage(inputs, {}, output));
    output = nullptr;
    auto fs = nau::io::createAssetPackFileSystem(nau::strings::toU8StringView(path), {.lifetimeOfCache = std::chrono::seconds(0)});
    auto file = fs->openFile("nested/data", nau::io::AccessMode::Read, nau::io::OpenFileMode::OpenExisting);
    ASSERT_TRUE(file);
    nau::io::IStreamReader::Ptr stream = file->createStream();
    std::string bytes(payload.size(), '\0');
    auto read = stream->read(reinterpret_cast<std::byte*>(bytes.data()), bytes.size());
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, payload.size());
    EXPECT_EQ(bytes, payload);
    stream = nullptr;
    file = nullptr;
    fs = nullptr;
    auto shutdown = runtime->shutdown();
    while (shutdown())
        std::this_thread::yield();
    std::filesystem::remove(path);
}
#endif
