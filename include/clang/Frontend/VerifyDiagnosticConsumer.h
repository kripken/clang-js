//===- VerifyDiagnosticConsumer.h - Verifying Diagnostic Client -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_VERIFYDIAGNOSTICSCLIENT_H
#define LLVM_CLANG_FRONTEND_VERIFYDIAGNOSTICSCLIENT_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include <climits>

namespace clang {

class DiagnosticsEngine;
class TextDiagnosticBuffer;
class FileEntry;

/// VerifyDiagnosticConsumer - Create a diagnostic client which will use
/// markers in the input source to check that all the emitted diagnostics match
/// those expected.
///
/// USING THE DIAGNOSTIC CHECKER:
///
/// Indicating that a line expects an error or a warning is simple. Put a
/// comment on the line that has the diagnostic, use:
///
///     expected-{error,warning,note}
///
/// to tag if it's an expected error or warning, and place the expected text
/// between {{ and }} markers. The full text doesn't have to be included, only
/// enough to ensure that the correct diagnostic was emitted.
///
/// Here's an example:
///
/// \code
///   int A = B; // expected-error {{use of undeclared identifier 'B'}}
/// \endcode
///
/// You can place as many diagnostics on one line as you wish. To make the code
/// more readable, you can use slash-newline to separate out the diagnostics.
///
/// Alternatively, it is possible to specify the line on which the diagnostic
/// should appear by appending "@<line>" to "expected-<type>", for example:
///
/// \code
///   #warning some text
///   // expected-warning@10 {{some text}}
/// \endcode
///
/// The line number may be absolute (as above), or relative to the current
/// line by prefixing the number with either '+' or '-'.
///
/// The simple syntax above allows each specification to match exactly one
/// error.  You can use the extended syntax to customize this. The extended
/// syntax is "expected-<type> <n> {{diag text}}", where \<type> is one of
/// "error", "warning" or "note", and \<n> is a positive integer. This allows
/// the diagnostic to appear as many times as specified. Example:
///
/// \code
///   void f(); // expected-note 2 {{previous declaration is here}}
/// \endcode
///
/// Where the diagnostic is expected to occur a minimum number of times, this
/// can be specified by appending a '+' to the number. Example:
///
/// \code
///   void f(); // expected-note 0+ {{previous declaration is here}}
///   void g(); // expected-note 1+ {{previous declaration is here}}
/// \endcode
///
/// In the first example, the diagnostic becomes optional, i.e. it will be
/// swallowed if it occurs, but will not generate an error if it does not
/// occur.  In the second example, the diagnostic must occur at least once.
/// As a short-hand, "one or more" can be specified simply by '+'. Example:
///
/// \code
///   void g(); // expected-note + {{previous declaration is here}}
/// \endcode
///
/// A range can also be specified by "<n>-<m>".  Example:
///
/// \code
///   void f(); // expected-note 0-1 {{previous declaration is here}}
/// \endcode
///
/// In this example, the diagnostic may appear only once, if at all.
///
/// Regex matching mode may be selected by appending '-re' to type. Example:
///
///   expected-error-re
///
/// Examples matching error: "variable has incomplete type 'struct s'"
///
///   // expected-error {{variable has incomplete type 'struct s'}}
///   // expected-error {{variable has incomplete type}}
///
///   // expected-error-re {{variable has has type 'struct .'}}
///   // expected-error-re {{variable has has type 'struct .*'}}
///   // expected-error-re {{variable has has type 'struct (.*)'}}
///   // expected-error-re {{variable has has type 'struct[[:space:]](.*)'}}
///
class VerifyDiagnosticConsumer: public DiagnosticConsumer,
                                public CommentHandler {
public:
  /// Directive - Abstract class representing a parsed verify directive.
  ///
  class Directive {
  public:
    static Directive *create(bool RegexKind, SourceLocation DirectiveLoc,
                             SourceLocation DiagnosticLoc,
                             StringRef Text, unsigned Min, unsigned Max);
  public:
    /// Constant representing n or more matches.
    static const unsigned MaxCount = UINT_MAX;

    SourceLocation DirectiveLoc;
    SourceLocation DiagnosticLoc;
    const std::string Text;
    unsigned Min, Max;

    virtual ~Directive() { }

    // Returns true if directive text is valid.
    // Otherwise returns false and populates E.
    virtual bool isValid(std::string &Error) = 0;

    // Returns true on match.
    virtual bool match(StringRef S) = 0;

  protected:
    Directive(SourceLocation DirectiveLoc, SourceLocation DiagnosticLoc,
              StringRef Text, unsigned Min, unsigned Max)
      : DirectiveLoc(DirectiveLoc), DiagnosticLoc(DiagnosticLoc),
        Text(Text), Min(Min), Max(Max) {
    assert(!DirectiveLoc.isInvalid() && "DirectiveLoc is invalid!");
    assert(!DiagnosticLoc.isInvalid() && "DiagnosticLoc is invalid!");
    }

  private:
    Directive(const Directive&); // DO NOT IMPLEMENT
    void operator=(const Directive&); // DO NOT IMPLEMENT
  };

  typedef std::vector<Directive*> DirectiveList;

  /// ExpectedData - owns directive objects and deletes on destructor.
  ///
  struct ExpectedData {
    DirectiveList Errors;
    DirectiveList Warnings;
    DirectiveList Notes;

    ~ExpectedData() {
      llvm::DeleteContainerPointers(Errors);
      llvm::DeleteContainerPointers(Warnings);
      llvm::DeleteContainerPointers(Notes);
    }
  };

private:
  typedef llvm::DenseSet<FileID> FilesWithDiagnosticsSet;
  typedef llvm::SmallPtrSet<const FileEntry *, 4> FilesWithDirectivesSet;

  DiagnosticsEngine &Diags;
  DiagnosticConsumer *PrimaryClient;
  bool OwnsPrimaryClient;
  OwningPtr<TextDiagnosticBuffer> Buffer;
  const Preprocessor *CurrentPreprocessor;
  FilesWithDiagnosticsSet FilesWithDiagnostics;
  FilesWithDirectivesSet FilesWithDirectives;
  ExpectedData ED;
  void CheckDiagnostics();

public:
  /// Create a new verifying diagnostic client, which will issue errors to
  /// the currently-attached diagnostic client when a diagnostic does not match 
  /// what is expected (as indicated in the source file).
  VerifyDiagnosticConsumer(DiagnosticsEngine &Diags);
  ~VerifyDiagnosticConsumer();

  virtual void BeginSourceFile(const LangOptions &LangOpts,
                               const Preprocessor *PP);

  virtual void EndSourceFile();

  virtual bool HandleComment(Preprocessor &PP, SourceRange Comment);

  virtual void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                                const Diagnostic &Info);
  
  virtual DiagnosticConsumer *clone(DiagnosticsEngine &Diags) const;
};

} // end namspace clang

#endif
