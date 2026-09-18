#include "Main.h"

#include "SaveCoordinator.h"

#include "Archive/Formats/WadArchive.h"
#include "Utility/MemChunk.h"

using namespace slade;

namespace slade_mobile
{

const char* SaveCoordinator::validateForSave(ArchiveSession& session) const
{
    if (!session.isOpen())
        return "Архив не открыт";

    auto* wad = session.archive();
    const unsigned expectedEntries = wad->numEntries();

    auto out = std::make_unique<MemChunk>();
    ArchiveSerializer serializer;
    if (!serializer.serialize(session, *out, /*allowIwadOverwrite=*/false))
        return global::error.c_str();

    MemChunk validateMc;
    validateMc.importMem(out->data(), static_cast<uint32_t>(out->size()));

    WadArchive validate;
    if (!validate.open(validateMc))
    {
        static std::string error;
        error = "Проверка не пройдена: " + global::error;
        return error.c_str();
    }

    if (validate.numEntries() != expectedEntries)
    {
        static std::string error;
        error = "Проверка не пройдена: ожидалось "
              + std::to_string(expectedEntries)
              + " entries, получено "
              + std::to_string(validate.numEntries());
        return error.c_str();
    }

    session.setPendingSave(std::move(out));
    return nullptr;
}

} // namespace slade_mobile
