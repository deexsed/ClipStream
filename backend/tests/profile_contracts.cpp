#include "database/Migrations.hpp"
#include "repositories/ProfileRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "utils/Environment.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace clipstream;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto name = utils::env("CLIPSTREAM_TEST_DATABASE");
        if (name.empty()) {
            std::cout << "Profile integration skipped\n";
            return 77;
        }
        if (!name.ends_with("_test"))
            throw std::runtime_error("Test database name required");
        database::DbConfig cfg{.host = utils::env("MYSQL_HOST", "127.0.0.1"),
                               .port = static_cast<uint16_t>(std::stoul(utils::env("MYSQL_PORT", "3306"))),
                               .database = name,
                               .user = utils::env("MYSQL_USER", "clipstream"),
                               .password = utils::env("MYSQL_PASSWORD"),
                               .pool_size = 1};
        database::ConnectionPool pool(cfg);
        database::migrate(pool, utils::env("MIGRATIONS_DIR", "database/migrations"));
        repositories::UserRepository users(pool);
        repositories::ProfileRepository profiles(pool);
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        models::User a, b;
        a.username = "profilea" + suffix;
        a.email = a.username + "@example.test";
        a.password_hash = "!";
        a.id = users.create(a);
        b.username = "profileb" + suffix;
        b.email = b.username + "@example.test";
        b.password_hash = "!";
        b.id = users.create(b);
        auto pa = profiles.ensure_user(a), pb = profiles.ensure_user(b);
        check(profiles.follow(a.id, pb.key), "Follow not created");
        check(!profiles.follow(a.id, pb.key), "Follow not idempotent");
        check(!profiles.follow(a.id, pa.key), "Self follow accepted");
        check(profiles.is_following(a.id, pb.key) && !profiles.is_following(b.id, pa.key),
              "Follow became bidirectional");
        check(users.friendship_status(a.id, b.id) == "none", "Follow created friendship");
        const auto tid = "900" + suffix;
        auto external =
            profiles.ensure_twitch(tid, "external", "External name", "https://example.test/avatar");
        check(external.user_id == 0, "External channel fabricated a local account");
        profiles.follow(a.id, external.key);
        {
            database::ScopedConnection conn(pool);
            database::query(
                conn.raw(),
                ("UPDATE users SET twitch_id='" + tid + "' WHERE id=" + std::to_string(b.id)).c_str());
        }
        b.twitch_id = tid;
        auto linked = profiles.ensure_user(b);
        check(linked.key == external.key && linked.user_id == b.id, "Twitch identity not unified");
        check(!profiles.find(pb.key), "Duplicate local profile survived merge");
        check(profiles.stats(linked)["followers"] == 1, "Merge duplicated follows");
        check(linked.display_name == "External name", "Site name overwrote Twitch identity");
        check(profiles.following(a.id, 25, 0).size() == 1, "Merged follows list incorrect");
        profiles.save_settings(b.id, {{"profile_visibility", "friends"},
                                      {"wall_policy", "friends"},
                                      {"following_visibility", "private"}});
        check(!profiles.may_view(b.id, a.id), "Nonfriend viewed private profile");
        check(users.send_friend_request(a.id, b.id) == 1, "Friend request failed");
        check(users.accept_friend_request(b.id, a.id), "Friend accept failed");
        check(profiles.may_view(b.id, a.id), "Friend cannot view friends profile");
        check(!profiles.may_view(b.id, a.id, "following_visibility"), "Private following list exposed");
        check(profiles.may_post_wall(b.id, a.id), "Friend wall permission failed");
        const auto post = users.create_wall_post(a.id, b.id, "Profile contract fixture");
        check(post.has_value(), "Allowed wall post rejected");
        using Mutation = repositories::ProfileRepository::WallMutation;
        check(profiles.mutate_wall(*post, b.id, false, false, "forbidden") == Mutation::Forbidden,
              "Profile owner edited someone else's post");
        check(profiles.mutate_wall(*post, a.id, false, false, "edited") == Mutation::Applied,
              "Author cannot edit post");
        check(profiles.mutate_wall(*post, b.id, false, true, "") == Mutation::Applied,
              "Profile owner cannot delete post");
        check(profiles.mutate_wall(*post, a.id, false, true, "") == Mutation::NotFound,
              "Deleted post still exists");
        profiles.save_settings(b.id,
                               {{"profile_visibility", "private"}, {"followers_visibility", "private"}});
        check(!profiles.may_view(b.id, a.id) && profiles.may_view(b.id, b.id),
              "Private profile authorization failed");
        profiles.save_settings(b.id, {{"bio", "Updated independently"}});
        check(profiles.settings(b.id)["followers_visibility"] == "private",
              "Partial update clobbered privacy");
        profiles.unfollow(a.id, linked.key);
        profiles.unfollow(a.id, linked.key);
        check(!profiles.is_following(a.id, linked.key), "Unfollow failed");
        users.remove(a.id);
        users.remove(b.id);
        std::cout << "Profile integration contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
