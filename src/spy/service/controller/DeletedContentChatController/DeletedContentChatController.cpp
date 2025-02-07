#include <spy/service/controller/DeletedContentChatController/DeletedContentChatController.hpp>
#include <oatpp/core/base/Environment.hpp>

#include <spy/service/controller/ChatsController/ChatsController.hpp>
#include <spy/service/controller/SettingsController/SpySettingsController.hpp>
#include <spy/service/controller/ControllersHandler.hpp>

#include <spy/utils/StringTools.h>

#include <spy/service/controller/DeletedContentChatController/Command/CommandHandler.hpp>
#include <spy/service/controller/DeletedContentChatController/Command/HelpCommand/HelpCommand.hpp>

#include <spy/utils/Logger/SpyLog.h>

void spy::service::controller::DeletedContentChatController::Initialize(const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    SPY_LOGD("DeletedContentChatController:Initialize");

    auto chatsController = this->service->GetController<spy::service::controller::ChatsController>();
    auto supergroups = chatsController->GetSupergroupChats();

    // Look for deleted content chat
    FindDeletedContentChat(supergroups, handler);

    // Create new deleted content chat
    if (deletedContentSupergroupChatId == 0 ) {
        SPY_LOGI("DeletedContentChatController:Initialize 'deleted content chat' not found. Create one..");
        CreateDeletedContentChat(handler);
    }
    else
        SPY_LOGD("DeletedContentChatController:Initialize Found 'deleted content chat'");

    // Find deleted content chat's creator
    GetDeletedContentChatOwner(handler);

    // Exclude chat from parsing
    ExcludeParwingDeletedContentChat(handler);

    initialized = true;

    SPY_LOGD("DeletedContentChatController:Initialize Finished chat: %d", deletedContentSupergroupChatId);
}

void spy::service::controller::DeletedContentChatController::RegisterUpdates(const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    SPY_LOGD("DeletedContentChatController:RegisterUpdates");
    handler->SetCallback<td::td_api::updateNewMessage>(false, [&](td::td_api::updateNewMessage& update) {
        onUpdateNewMessage(update, handler);
    });
}

void spy::service::controller::DeletedContentChatController::onUpdateNewMessage(td::td_api::updateNewMessage& update, const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    if (!initialized) return;

    // Skip message if is not in deleted content chat or have no permission
    if (update.message_->chat_id_ != deletedContentSupergroupChatId || !canProcessCommands) return;

    // Return if can not process message text
    if (update.message_->content_->get_id() != td::td_api::messageText::ID) return;
    auto messageText = static_cast<td::td_api::messageText&>(*update.message_->content_).text_->text_;


    // Process cancel command
    if (this->command && command::getCommandName(messageText) == "/cancel") {
        this->command->Cencel();
        this->command.reset();
    }

    // If command was empty
    else if (!this->command) {
        if (StringTools::startsWith(messageText, "/")) {
            this->command = command::matchCreateCommand(messageText, handler, this->service);
        }
    }

    // Process command
    if (this->command) {
        this->command->Process(messageText);

        if (this->command->IsDone()) {
            this->command.reset();
        }
    }
}

void spy::service::controller::DeletedContentChatController::FindDeletedContentChat(const std::vector<std::int64_t>& chat_ids, const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    SPY_LOGD("DeletedContentChatController:FindDeletedContentChat");

    std::condition_variable condition;
    std::int64_t loadedChats = 0;

    // Look for deleted content chat
    for (auto groupId : chat_ids) {
        auto func = td::td_api::make_object<td::td_api::getChat>(groupId);

        auto tick = [&](tdlpp::SharedObjectPtr<td::td_api::Object> object) {
            td::td_api::downcast_call(*object, tdlpp::overloaded(
                [&](td::td_api::chat& chat) {
                    // printf("   Chat title: %s\n", chat.title_.c_str());
                    if (chat.title_ == SPY_DELETED_CONTENT_CHAT_TITLE) {
                        deletedContentSupergroupChatId = chat.id_;

                        td::td_api::downcast_call(*chat.type_, tdlpp::overloaded(
                            [&](td::td_api::chatTypeSupergroup& spg) {
                                deletedContentSupergroupId = spg.supergroup_id_;
                            },
                            [](auto&) { }
                        ));
                    }
                },
                [](auto&) {}
            ));

            loadedChats++;
            condition.notify_all();
        };

        handler->ExecuteAsync<td::td_api::getChat>(std::move(func), tick);
    }

    std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);
    condition.wait(lock, [&]() -> bool { return loadedChats == chat_ids.size(); } );
}

void spy::service::controller::DeletedContentChatController::CreateDeletedContentChat(const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    SPY_LOGD("DeletedContentChatController:CreateDeletedContentChat");

    // Create supergroup
    auto supergroupPromise = handler->Execute<td::td_api::createNewSupergroupChat>(
        SPY_DELETED_CONTENT_CHAT_TITLE,
        false,
        SPY_DELETED_CONTENT_CHAT_DESCRIPTION,
        nullptr,
        false
    );

    // Handle error
    if (supergroupPromise->GetResponse()->get_id() == td::td_api::error::ID) {
        auto error = tdlpp::cast_object<td::td_api::error>(supergroupPromise->GetResponse());
        SPY_LOGF("DeletedContentChatController:CreateDeletedContentChat td_api::createNewSupergroupChat: %s", error.message_.c_str());
    }


    // Set group id locally
    auto deletedContentChat = tdlpp::cast_object<td::td_api::chat>(supergroupPromise->GetResponse());
    deletedContentSupergroupChatId = deletedContentChat.id_;

    td::td_api::downcast_call(*deletedContentChat.type_, tdlpp::overloaded(
        [&](td::td_api::chatTypeSupergroup& spg) {
            deletedContentSupergroupId = spg.supergroup_id_;
        },
        [](auto&) { }
    ));


    // Set chat photo
    auto setPhotoPromise = handler->Execute<td::td_api::setChatPhoto>(
        deletedContentSupergroupChatId,
        td::td_api::make_object<td::td_api::inputChatPhotoStatic>(
            td::td_api::make_object<td::td_api::inputFileLocal>("resources/spy-icon.png")
        )
    );

    // Send sticker from brand 'Spy X telegram' sticker set
    auto sendStickerPromise = handler->Execute<td::td_api::sendMessage>(
        deletedContentSupergroupChatId,
        0,
        0,
        td::td_api::make_object<td::td_api::messageSendOptions>(false, true, false, nullptr),
        nullptr,
        td::td_api::make_object<td::td_api::inputMessageSticker>(
            td::td_api::make_object<td::td_api::inputFileRemote>("CAACAgIAAx0EXAlmkQADTGK9_8JzIn9-W-hN0nJXcUQfuyT1AALZGQACh_3pSbnBN_krgju0KQQ"),
            nullptr,
            512,
            512,
            "👀"
        )
    );

    // Handle error
    if (setPhotoPromise->GetResponse()->get_id() == td::td_api::error::ID) {
        auto error = tdlpp::cast_object<td::td_api::error>(setPhotoPromise->GetResponse());
        SPY_LOGE("DeletedContentChatController:CreateDeletedContentChat td_api::setChatPhoto: %s", error.message_.c_str());
    }

    // Handle error
    if (sendStickerPromise->GetResponse()->get_id() == td::td_api::error::ID) {
        auto error = tdlpp::cast_object<td::td_api::error>(sendStickerPromise->GetResponse());
        SPY_LOGE("DeletedContentChatController:CreateDeletedContentChat td_api::sendMessage: %s", error.message_.c_str());
    }

    // Send help info
    auto helpCommand = command::HelpCommand::makeCommand(handler, this->service);
    helpCommand->Process("/help");
}

void spy::service::controller::DeletedContentChatController::ExcludeParwingDeletedContentChat(const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    SPY_LOGD("DeletedContentChatController:ExcludeParwingDeletedContentChat");

    // Exclude deleted content chat from parsing
    auto settings = this->service->GetController<spy::service::controller::SpySettingsController>();
    auto excludeChats = settings->GetExcludeChats();

    // Add saved messages to excluded chats if wasn't added
    if (std::find(excludeChats.begin(), excludeChats.end(), (int)deletedContentSupergroupChatId) == excludeChats.end()) {
        excludeChats.emplace_back((int)deletedContentSupergroupChatId);
        settings->SetExcludeChats(excludeChats);
    }
}

void spy::service::controller::DeletedContentChatController::GetDeletedContentChatOwner(const std::shared_ptr<tdlpp::base::TdlppHandler>& handler) {
    SPY_LOGD("DeletedContentChatController:GetDeletedContentChatOwner");

    // Get me to access my id
    auto mePromise = handler->Execute<td::td_api::getMe>();
    auto me = tdlpp::cast_object<td::td_api::user>(mePromise->GetResponse());

    // Request administrators of deleted content chat 
    auto adminsPromise = handler->Execute<td::td_api::getSupergroupMembers>(
        deletedContentSupergroupId,
        std::move(td::td_api::make_object<td::td_api::supergroupMembersFilterAdministrators>()),
        0, 10
    );

    if (adminsPromise->GetResponse()->get_id() == td::td_api::error::ID) {
        auto error = tdlpp::cast_object<td::td_api::error>(adminsPromise->GetResponse());
        SPY_LOGF("DeletedContentChatController:GetDeletedContentChatOwner td_api::getSupergroupMembers: %s", error.message_.c_str());
    }

    auto admins = tdlpp::cast_object<td::td_api::chatMembers>(adminsPromise->GetResponse());

    // Iterete admins. Determin if I can process messages
    for (auto& admin : admins.members_) {
        // Chech if member is a chat creator
        td::td_api::downcast_call(*admin->status_, tdlpp::overloaded(
            [&](td::td_api::chatMemberStatusCreator& creator) {
                // Check creators id
                td::td_api::downcast_call(*admin->member_id_, tdlpp::overloaded(
                    [&](td::td_api::messageSenderUser& user) {
                        if (me.id_ == user.user_id_) canProcessCommands = true;
                    },
                    [](auto&) { }
                ));
            },
            [](auto&) { }
        ));
    }
}


std::int64_t spy::service::controller::DeletedContentChatController::GetDeletedContentChatId() {
    return deletedContentSupergroupChatId;
}
