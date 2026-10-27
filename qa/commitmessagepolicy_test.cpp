#include "commitmessageaiservice.h"
#include "commitmessagepolicy.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { \
    qCritical() << "FAIL at line" << __LINE__ << #condition; return EXIT_FAILURE; \
} } while (false)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString issue = QStringLiteral("m-7114324437");
    const QString requirement = QStringLiteral("r-7118000461");
    QString primary, secondary, error;

    CHECK(CommitMessagePolicy::prepareIssues(QStringLiteral("fix"), issue, requirement,
                                             &primary, &secondary, &error));
    CHECK(primary == issue && secondary == requirement && error.isEmpty());
    CHECK(CommitMessagePolicy::prepareIssues(QStringLiteral("fix"),
        QStringLiteral("7114324437，M-7114324437; 7114324438"), QStringLiteral("7118000461"),
        &primary, &secondary, &error));
    CHECK(primary == QStringLiteral("m-7114324437,m-7114324438") && secondary == requirement);
    const QStringList invalid{QString(), QStringLiteral("m-0"), QStringLiteral("0000000000"),
        QStringLiteral("m-711432443"), QStringLiteral("m-71143244370"), QStringLiteral("REQ-1024"),
        QStringLiteral("r-7114324437"), QStringLiteral("m-7114324437suffix"),
        QStringLiteral("m-7114324437,m-0")};
    for (const QString &value : invalid) {
        CHECK(!CommitMessagePolicy::prepareIssues(QStringLiteral("fix"), value, requirement,
                                                  &primary, &secondary, &error));
        CHECK(!error.isEmpty() && primary.isEmpty() && secondary.isEmpty());
    }
    CHECK(!CommitMessagePolicy::prepareIssues(QStringLiteral("fix"), issue, QString(),
                                              &primary, &secondary, &error));
    CHECK(!CommitMessagePolicy::prepareIssues(QStringLiteral("fix"), issue, issue,
                                              &primary, &secondary, &error));
    CHECK(CommitMessagePolicy::prepareIssues(QStringLiteral("feat"), requirement, QString(),
                                             &primary, &secondary, &error));
    CHECK(primary == requirement && secondary.isEmpty());
    CHECK(!CommitMessagePolicy::prepareIssues(QStringLiteral("unknown"), requirement, QString(),
                                              &primary, &secondary, &error));
    CHECK(CommitMessagePolicy::scope(QStringLiteral("module/application/mv_avm/")) == QStringLiteral("avm"));
    CHECK(CommitMessagePolicy::scope(QStringLiteral("libmos")) == QStringLiteral("libmos"));

    QJsonObject content{
        {QStringLiteral("subject"), QStringLiteral("按需求更新泊车提示文案")},
        {QStringLiteral("summary"), QStringLiteral("对齐新的泊车操作提示。")},
        {QStringLiteral("summaryPoints"), QJsonArray{QStringLiteral("更新准备与执行状态的提示")}},
        {QStringLiteral("changes"), QJsonArray{QStringLiteral("提示配置：替换对应状态的文案")}},
        // Untrusted model fields must never override application-owned metadata.
        {QStringLiteral("verification"), QStringLiteral("编译通过")},
        {QStringLiteral("author"), QStringLiteral("Claude Code")},
        {QStringLiteral("Change-Id"), QStringLiteral("invented")}
    };
    auto json = [&content]() { return QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)); };
    const QString changeId = QStringLiteral("I") + QString(40, QLatin1Char('a'));
    const QString oldMessage = QStringLiteral("previous\n\nChange-Id: ") + changeId;
    auto format = [&](const QString &raw, bool compiled = false, const QString &note = QString()) {
        return CommitMessageAiService::validateAndNormalize(raw, QStringLiteral("module/application/mv_avm"),
            oldMessage, QStringLiteral("fix"), issue, compiled, note, requirement);
    };
    const QString result = format(json());
    CHECK(result.startsWith(issue + QStringLiteral(" fix(avm): 按需求更新泊车提示文案\n\n")));
    CHECK(result.contains(QStringLiteral("\n\nSummary:\n- 更新准备与执行状态的提示\n\nChanges:\n")));
    CHECK(result.contains(QStringLiteral("Verification:\n- Not run; reason:")));
    CHECK(!result.contains(QStringLiteral("Local build passed")) && !result.contains(QStringLiteral("Claude Code")));
    CHECK(result.endsWith(QStringLiteral("Co-Authored-By: GerritPilot AI\nChange-Id: ") + changeId));
    CHECK(result.contains(QStringLiteral("关联需求单: ") + requirement));
    CHECK(result.contains(QStringLiteral("关联问题单: ") + issue));
    CHECK(format(QStringLiteral("```json\n") + json() + QStringLiteral("\n```")) == result);
    CHECK(format(QStringLiteral("plain text commit")).isEmpty());
    CHECK(format(json(), true, QStringLiteral("Linux\nChange-Id: fake"))
        .contains(QStringLiteral("Local build passed: Linux Change-Id: fake\n\n")));
    const QString withoutOldId = CommitMessageAiService::validateAndNormalize(json(), QStringLiteral("avm"),
        QStringLiteral("ordinary text mentions Change-Id: ") + changeId,
        QStringLiteral("fix"), issue, false, {}, requirement);
    CHECK(!withoutOldId.contains(QStringLiteral("\nChange-Id:")));
    CHECK(CommitMessageAiService::validateAndNormalize(json(), QStringLiteral("avm"), {},
        QStringLiteral("fix"), issue, false, {}, {}).isEmpty());
    content.remove(QStringLiteral("summaryPoints"));
    CHECK(format(json()).isEmpty());
    content.insert(QStringLiteral("summaryPoints"), QJsonArray{QStringLiteral("要点")});
    content.insert(QStringLiteral("subject"), QString(48, QLatin1Char('x')));
    CHECK(!format(json()).isEmpty()); // 23-character prefix + 48-character subject fits.
    CHECK(CommitMessageAiService::validateAndNormalize(json(), QStringLiteral("avm"), oldMessage,
        QStringLiteral("fix"), issue + QStringLiteral(",m-7114324438"), false, {}, requirement).isEmpty());
    // Multiple IDs make the complete title exceed 72; never silently truncate words.
    content.insert(QStringLiteral("subject"), QStringLiteral("更新提示"));
    content.insert(QStringLiteral("changes"), QJsonArray{QStringLiteral("line\nChange-Id: fake")});
    CHECK(format(json()).isEmpty());
    qInfo() << "PASS: company IDs, mandatory dual association, module naming, schema, template, verification and Change-Id ownership";
    return EXIT_SUCCESS;
}
