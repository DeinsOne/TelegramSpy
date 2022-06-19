
#ifndef spy_MessagesDb
#define spy_MessagesDb

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/core/Types.hpp>

#include <oatpp/orm/DbClient.hpp>
#include <oatpp/orm/SchemaMigration.hpp>
#include <oatpp-sqlite/orm.hpp>

#include <td/telegram/td_api.h>

#include <spy/dto/MessageModification.hpp>

#include OATPP_CODEGEN_BEGIN(DbClient)      //<- Begin Codegen

namespace spy { namespace db {

    class MessagesDb : public oatpp::orm::DbClient {
    public:
        MessagesDb(const std::shared_ptr<oatpp::orm::Executor>& executor) : oatpp::orm::DbClient(executor) {
            oatpp::orm::SchemaMigration migration(executor);

            migration.addFile(1 /* start from version 1 */, DATABASE_MIGRATIONS "messages-migration.sql");
            migration.addFile(2, DATABASE_MIGRATIONS "messages-content.sql");

            migration.migrate();        // <-- run migrations. This guy will throw on error.

            auto version = executor->getSchemaVersion();
        }

    public:
        void AddMessage(td::td_api::message& message);
        void AddMessageModification(const std::int64_t& message_id, const std::int64_t& chat_id, td::td_api::MessageContent& content);

        // void AddDeletedModification(const std::int64_t& message_id, const std::int64_t& chat_id);
        void AddDeletedModifications(const std::vector<std::int64_t>& message_ids, const std::int64_t& chat_id);

        void AddFile(const std::string& id, const std::string& path, const std::size_t& size);

    private:
        void addBaseMessage(td::td_api::message& message);
        void addMessageModification(const std::int64_t& message_id, const std::int64_t& chat_id, const std::int32_t& version, td::td_api::MessageContent& content);
        void addMessageContent(const std::int64_t& message_id, const std::int64_t& chat_id, const std::int32_t& version, td::td_api::MessageContent& content);
        void addDeletedModification(const std::int64_t& message_id, const std::int64_t& chat_id);

    };

} } // namespace spy { namespace db

#include OATPP_CODEGEN_END(DbClient)        //<- End Codegen

#endif // spy_MessagesDb
