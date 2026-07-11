#include "JuceHeader.h"

class FormValidationTest final : public juce::UnitTest {
public:
    FormValidationTest()
        : juce::UnitTest("Form validation", "Validation") {}

    void initialise() override {
        juce::MessageManager::getInstance();
    }

    void runTest() override {
        beginTest("String rules compose in declaration order");
        auto schema = osci::StringValidation().trim()
                          .required("Required")
                          .minLength(3, "Too short")
                          .maxLength(8, "Too long");
        auto error = schema.validate("   ");
        expect(error.has_value());
        expectEquals(error->code, juce::String("required"));
        expectEquals(error->message, juce::String("Required"));
        error = schema.validate(" ab ");
        expect(error.has_value());
        expectEquals(error->code, juce::String("min_length"));
        expect(!schema.validate(" valid ").has_value());

        beginTest("Email validation accepts common addresses");
        const juce::StringArray validEmails {
            "person@example.com",
            "first.last+tag@sub.example.co.uk",
            "o'hara@example.org",
            "user_name@example.io",
        };
        for (const auto& email : validEmails) {
            expect(osci::StringValidation::isValidEmail(email), email);
        }

        beginTest("Email validation rejects malformed addresses");
        const juce::StringArray invalidEmails {
            "",
            " person@example.com ",
            "person",
            "@example.com",
            "person@@example.com",
            ".person@example.com",
            "person.@example.com",
            "person..name@example.com",
            "person@example",
            "person@-example.com",
            "person@example-.com",
            "person@example.c",
            "person@example.123",
        };
        for (const auto& email : invalidEmails) {
            expect(!osci::StringValidation::isValidEmail(email), email);
        }

        beginTest("Custom rules are reusable");
        auto prefixed = osci::StringValidation().custom(
            "prefix", "Must start with osci-", [](const juce::String& value) { return value.startsWith("osci-"); });
        expect(!prefixed.validate("osci-render").has_value());
        expectEquals(prefixed.validate("render")->code, juce::String("prefix"));

        beginTest("Form results preserve registration order and field state");
        juce::TextEditor emailEditor;
        juce::TextEditor titleEditor;
        osci::FormValidator form;
        form.registerField("email", emailEditor,
                           osci::StringValidation().trim().required("Email required").email("Email invalid"));
        form.registerField("title", titleEditor,
                           osci::StringValidation().trim().required("Title required"));

        auto result = form.validate(false);
        expect(!result.isValid());
        expectEquals(result.getIssueCount(), 2);
        expectEquals(result.getFirstIssue()->field, juce::String("email"));
        expect(form.hasBeenSubmitted());
        const auto emailState = form.getFieldState("email");
        expect(emailState.has_value());
        expect(emailState->validated);
        expect(emailState->error.has_value());

        emailEditor.setText("person@example.com", true);
        titleEditor.setText("Feedback title", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        result = form.getResult();
        expect(result.isValid());

        beginTest("Manual errors support server-side validation");
        expect(form.setError("email", { "server", "Email could not be verified" }));
        expectEquals(form.getResult().getIssueCount(), 1);
        expect(form.clearError("email"));
        expect(form.getResult().isValid());
        form.resetValidation();
        expect(!form.hasBeenSubmitted());
    }
};

static FormValidationTest formValidationTest;
